#include "deftermv2.hpp"

#include <windows.h>
#include <cassert>
#include <utility>
#include "connect_policy.hpp"
#include "io.hpp"
#include "terminal_handoff.hpp"
#include "vt_handles.hpp"
#include "conpty.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/env.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

enum class initial_connect_completion
{
    explicit_complete,
    inline_complete,
};

struct connect_handler
{
    // false: 还没处理任何 CONNECT，首个可见控制台进程仍可触发 GUI handoff。
    // true : 已处理过首个 CONNECT，后续 CONNECT 只能复用既有会话。
    bool initialized = false;

    // false: 初始循环继续由 deftermv2 处理 handoff/fallback。
    // true : 初始 CONNECT 已在本对象中 accept，入口函数应启动 conpty session。
    bool start_conpty = false;

    // 0 表示 CONNECT 没提供有效尺寸，conpty 使用 default_console_size。
    // 正数来自 CONSOLE_SERVER_MSG::ScreenBufferSize。
    short width = 0;
    short height = 0;

    // 0 表示还没有已 attach 的客户端进程；非 0 是首个 CONNECT 的 pid，
    // 用于初始化 libcorehost 的进程列表，保证 GetConsoleProcessList 可见。
    DWORD attached_process_id = 0;

    // miniio::accept_connection 打开的 ConDrv 客户端句柄。它们必须在
    // conpty/mini fallback 生命周期内保持有效，否则客户端 ReadFile/WriteFile
    // 会收到断开。
    win32::handle condrv_input;
    win32::handle condrv_output;

    // server/input_event 不拥有句柄，只引用 deftermv2_entry 中的 RAII 对象。
    // connect_handler 不得在这些 view 失效后继续运行。
    win32::handle_view server;
    win32::handle_view input_event;

    bool on_connect(miniio::io_msg &msg, initial_connect_completion &completion)
    {
        // explicit_complete 表示本函数不在当前 READ_IO 返回体中填 completion；
        // 需要 inline 完成的分支会覆盖它。
        completion = initial_connect_completion::explicit_complete;

        // client_pid 来自 CD_IO_DESCRIPTOR::Process。它必须是当前 CONNECT
        // 的客户端 pid，后续通知、进程列表和 CTRL_BREAK 都依赖它。
        const auto client_pid = static_cast<DWORD>(msg.descriptor.Process);

        // CONNECT 是初始路径唯一直接解析的消息体；ConDrv 保证 body 至少
        // 包含 CONSOLE_SERVER_MSG。
        auto &connect_info = *reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body);
        LOG("CONNECT received: pid=%lu pgid=%lu initialized=%d consoleApp=%u visible=%u show=%u flags=0x%08lx",
            client_pid, connect_info.ProcessGroupId, initialized, static_cast<unsigned>(connect_info.ConsoleApp),
            static_cast<unsigned>(connect_info.WindowVisible), connect_info.ShowWindow, connect_info.StartupFlags);

        // need_gui 为 true 时尝试默认终端 COM handoff；false 时 CONNECT 仍会
        // 被 accept，但会直接进入 headless conpty 会话。
        const bool need_gui = should_start_terminal_window(connect_info, initialized);
        initialized = true;

        if (!need_gui)
        {
            LOG("GUI not expected; accepting CONNECT and starting headless conpty");

            // ScreenBufferSize 中非正值没有可用意义，保留 0 让 libcorehost
            // 采用统一默认尺寸。
            width = connect_info.ScreenBufferSize.X > 0 ? connect_info.ScreenBufferSize.X : 0;
            height = connect_info.ScreenBufferSize.Y > 0 ? connect_info.ScreenBufferSize.Y : 0;
            attached_process_id = client_pid;
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            start_conpty = true;
            return false;
        }

        if (env::is_elevated())
        {
            LOG("elevated process cannot handoff to user terminal; notification and immediate CTRL_BREAK expected");

            // image_path 只用于通知；查询失败会抛出，避免显示空程序路径。
            auto image_path = query_process_image_path(client_pid);
            LOG(L"elevated client image path: %ls", image_path.c_str());
            env::show_elevated_notification(image_path);
            miniio::accept_connection(server, msg, condrv_input, condrv_output);

            // ProcessGroupId 为 0 时没有显式进程组，只能用 pid 作为
            // GenerateConsoleCtrlEvent 的目标。
            const DWORD target_process_group_id =
                connect_info.ProcessGroupId ? connect_info.ProcessGroupId : client_pid;
            LOG("sending immediate CTRL_BREAK to process group=%lu", target_process_group_id);
            ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, target_process_group_id);
            return true;
        }

        LOG("GUI expected; trying default-terminal COM handoff (failure is allowed)");
        if (try_terminal_handoff(server, input_event, make_portable_attach_msg(msg), client_pid))
        {
            LOG("terminal handoff completed; initial loop should exit");
            return false;
        }

        LOG("no terminal accepted handoff; notification and CTRL_BREAK are expected fallback");
        env::show_not_found_notification();
        miniio::accept_connection(server, msg, condrv_input, condrv_output);
        // 没有可用终端时立即打断客户端进程组，避免它永久等待不可见控制台。
        ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
        return true;
    }

    bool on_message(miniio::io_msg &msg)
    {
        LOG3("fallback message: func=%lu", msg.descriptor.Function);
        dispatch_console(server, msg, condrv_input, condrv_output);
        return true;
    }

    void on_idle() noexcept
    {
    }

    bool has_pending() const noexcept
    {
        // 当前 handler 没有自有异步操作；false 让主循环只等待 ConDrv 事件。
        return false;
    }

    bool should_exit() const noexcept
    {
        // 初始循环的退出条件由 CONNECT 分支返回 false 或 ConDrv 断开决定。
        return false;
    }
};

void run_initial_connect_loop(win32::handle_view server, win32::handle_view input_event, connect_handler &handler)
{
    LOG3("initial CONNECT loop started; waiting for ConDrv messages server=%p event=%p", server.get(),
         input_event.get());
    miniio::set_server_info(server, input_event);

    // ConDrv 的 READ_IO 同时提交上一条 completion 并读取下一条消息。
    // 因此需要双缓冲，避免 completion 的 Write.Data 指向的 body 被下一条
    // 读取覆盖。
    miniio::io_msg message_a{};
    miniio::io_msg message_b{};

    // current 指向本轮接收缓冲，只能是 message_a 或 message_b。
    miniio::io_msg *current = &message_a;

    // nullptr 表示本轮 READ_IO 不提交 completion；非 nullptr 指向上一条
    // 已由 handler 填好 complete 字段的消息。
    miniio::io_msg *completed_previous = nullptr;

    for (;;)
    {
        if (!handler.has_pending() && completed_previous == nullptr)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                // 16ms 只用于空闲节流，避免没有消息时空转。
                ::WaitForSingleObject(input_event.get(), 16);
        }

        // completion 为 nullptr 或 completed_previous->complete。READ_IO 返回
        // no_message 时，驱动已经消费该 completion，下一轮不能重复提交。
        auto *completion = completed_previous ? &completed_previous->complete : nullptr;
        if (completion)
        {
            LOG3("submitting previous completion id=%08lx:%08lx status=0x%08lx info=%llu",
                 completion->Identifier.HighPart, completion->Identifier.LowPart,
                 static_cast<unsigned long>(completion->IoStatus.Status),
                 static_cast<unsigned long long>(completion->IoStatus.Information));
        }
        else
        {
            LOG3("reading next message without completion");
        }

        const auto read_result = miniio::read_io_try(server, completion, *current);
        if (read_result == miniio::read_io_result::disconnected)
        {
            LOG3("ConDrv disconnected; initial loop will exit");
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            completed_previous = nullptr;
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }
        completed_previous = nullptr;

        LOG3("message received: func=%lu id=%08lx:%08lx pid=%llu object=%llu in=%lu out=%lu",
             current->descriptor.Function, current->descriptor.Identifier.HighPart,
             current->descriptor.Identifier.LowPart, static_cast<unsigned long long>(current->descriptor.Process),
             static_cast<unsigned long long>(current->descriptor.Object), current->descriptor.InputSize,
             current->descriptor.OutputSize);

        if (current->descriptor.Function == no_console_io_function)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }

        if (current->descriptor.Function == CONSOLE_IO_CONNECT)
        {
            initial_connect_completion connect_result = initial_connect_completion::explicit_complete;
            if (!handler.on_connect(*current, connect_result))
            {
                LOG3("CONNECT handler completed session handoff; initial loop will return");
                return;
            }
            completed_previous = connect_result == initial_connect_completion::inline_complete ? current : nullptr;
            current = current == &message_a ? &message_b : &message_a;
            continue;
        }

        if (handler.on_message(*current))
        {
            completed_previous = current;
            handler.on_idle();
            if (handler.should_exit())
                break;
        }
        else
        {
            while (handler.has_pending())
            {
                handler.on_idle();
                if (handler.has_pending())
                    // 16ms 约等于一帧；只在 handler 自己仍有 pending 工作时让步。
                    ::WaitForSingleObject(input_event.get(), 16);
            }
            if (handler.should_exit())
                break;
        }
        current = current == &message_a ? &message_b : &message_a;
    }

    LOG3("initial CONNECT loop exited");
}

void deftermv2_entry(std::uintptr_t condrv_handle)
{
    LOG("entry started with ConDrv handle=0x%Ix", condrv_handle);
    assert(condrv_handle != 0);

    auto server = win32::handle{reinterpret_cast<HANDLE>(condrv_handle)};

    // 传给 ConDrv 的 InputAvailableEvent。初始循环注册它；转入 conpty 时
    // 通过 release() 交给 libcorehost 继续使用同一个事件对象。
    auto input_event = win32::event{win32::create_tag, true, false};

    connect_handler handler{};

    // handler 只借用 server/input_event；run_initial_connect_loop 返回前二者
    // 都在当前栈帧中保持有效。
    handler.server = server.view();
    handler.input_event = input_event.view();

    run_initial_connect_loop(server.view(), input_event.view(), handler);
    LOG("initial loop returned: startConpty=%d", handler.start_conpty);

    LOG_IF(!handler.start_conpty, "no conpty session expected; entry returns");
    if (!handler.start_conpty)
        return;

    win32::handle vt_in;
    win32::handle vt_out;

    // 仅在没有真实 STDIN 时持有 CreatePipe 的写端，避免读端立即 EOF。
    // 它不传给 libcorehost，但 run_conpty_session 返回前必须保持存活。
    win32::handle vt_in_keepalive;
    initialize_vt_handles(vt_in, vt_out, vt_in_keepalive);

    // config 只描述本次 conpty 会话的策略，不拥有任何句柄。width/height 为
    // 0 时 run_conpty_session 使用 default_console_size。
    conpty::conpty_session_config config;
    config.width = handler.width;
    config.height = handler.height;
    config.text_measurement = conpty::text_measurement_mode::graphemes;
    config.ambiguous_is_wide = true;
    config.poll_vt_input = vt_in_keepalive.valid();
    config.attached_process_id = handler.attached_process_id;

    LOG("starting conpty session: size=%dx%d attachedPid=%lu pollVtInput=%d", config.width, config.height,
        config.attached_process_id, config.poll_vt_input);
    conpty::run_conpty_session(std::move(server), win32::handle{input_event.release()}, std::move(handler.condrv_input),
                               std::move(handler.condrv_output), std::move(vt_in), std::move(vt_out), {}, config);
}

} // namespace deftermv2
