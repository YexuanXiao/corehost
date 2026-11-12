// ── conpty/io_state.hpp ──────────────────────────
// Layer 2: ConDrv 连接和对象句柄状态。
//
// 功能分解：
// 1. CONNECT：首个客户端连接由 accept_connection 创建 \Input/\Output；
//    子进程后续 CONNECT 只完成请求，继续共享首个连接的句柄。
// 2. 进程列表：记录已 CONNECT 但未 DISCONNECT 的 pid，供
//    GetConsoleProcessList 返回当前控制台进程集合。
// 3. CREATE/CLOSE_OBJECT：根据 ConDrv 对象类型创建新的客户端句柄，并
//    缓存最近返回的 input/output 句柄值用于 CLOSE_OBJECT 识别。
#pragma once
#include <windows.h>
#include <cstddef>
#include "win32/handle.hpp"
#include "condrv_io.hpp"
#include "process_registry.hpp"
#include "ntapi/condrv.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"

namespace corehost::conpty
{

using namespace condrv_io::error;

struct io_state
{
    enum class object_kind
    {
        // descriptor.Object 没有对应到当前缓存的 input/output 客户端句柄。
        unknown,
        // descriptor.Object 对应当前 input 客户端句柄。
        input,
        // descriptor.Object 对应当前主 output 客户端句柄。
        output,
        // descriptor.Object 对应新建 output screen buffer 句柄。
        alternate_output,
    };

    // 必须指向 ConDrv \Server；CREATE_OBJECT 和首个 CONNECT 使用它创建
    // \Input/\Output 客户端句柄。
    win32::handle_view server;

    // 首个 CONNECT 成功后由 accept_connection 填充。空句柄表示还没有
    // 完成客户端连接；非空时必须保持到会话结束，否则客户端 I/O 会断开。
    win32::handle condrv_input;
    win32::handle condrv_output;

    // 最近一次 CREATE_OBJECT 返回给 ConDrv 的 input/output 句柄值。
    // 0 表示当前没有对应对象；CLOSE_OBJECT 使用它识别被关闭的对象。
    ULONG_PTR input_id = 0;
    ULONG_PTR output_id = 0;
    ULONG_PTR alternate_output_id = 0;

    // 绑定 ConDrv server 非拥有引用。真实句柄由会话入口持有，io_state 只用
    // 它创建客户端对象和 accept 首个 CONNECT。
    void set_server(win32::handle_view s) noexcept
    {
        // s 不转移所有权；run_conpty_session 持有真实 server 句柄直到会话结束。
        server = s;
    }

    // 最近一次 CONNECT 的客户端 pid。0 表示尚未收到 CONNECT。
    ULONG_PTR client_pid = 0;

    // 当前控制台已连接进程（pid + 进程组）的单一真相。CONNECT/DISCONNECT 在
    // 会话锁内修改；pipe_bridge 通过 process_list() 读取同一对象（不再复制快照）。
    process_registry processes;

    [[nodiscard]] process_registry &process_list() noexcept
    {
        return processes;
    }

    [[nodiscard]] const process_registry &process_list() const noexcept
    {
        return processes;
    }

    // 消费一条 CONNECT 消息，把回复写进 msg.complete 但不提交。
    //
    // 两种 CONNECT 都走 piggyback：completion 由 message_router 的下一轮 READ_IO
    // 携带提交，因此每条 CONNECT 都省掉一次 COMPLETE_IO 往返。
    void handle_connect(condrv_io::io_msg &msg)
    {
        // msg 的 body 对 CONNECT 不由本层解析；已完成握手后它会被回复载荷
        // 覆盖。descriptor.Process 是 ConDrv 记录的客户端 pid，必须能放入
        // DWORD，因为 Win32 进程列表 API 以 DWORD 表示 pid。
        client_pid = msg.descriptor.Process;
        const bool first_connect = !condrv_input.valid();

        // CONNECT 请求体带 CONSOLE_SERVER_MSG，其中 ProcessGroupId 是
        // Ctrl+C / Ctrl+Break 的投递目标。必须在 accept_connection 覆盖
        // msg.body 之前读。
        DWORD client_group = static_cast<DWORD>(client_pid);
        if (msg.descriptor.InputSize >= sizeof(CONSOLE_SERVER_MSG))
        {
            const auto *connect_info = reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body);
            if (connect_info->ProcessGroupId != 0)
                client_group = connect_info->ProcessGroupId;
        }
        // first_connect 决定是否需要真正 accept \Input/\Output；后续 CONNECT
        // 只增加进程列表，不替换已有控制台对象句柄。
        LOG2("handle_connect pid=%llu first=%d processCount=%zu", client_pid, first_connect, processes.count());

        // 同一 pid 可能重复 CONNECT；processes.add 会去重，避免
        // GetConsoleProcessList 返回重复项。
        processes.add(static_cast<DWORD>(client_pid), client_group);
        if (first_connect)
        {
            // 首个 CONNECT 需要创建 \Input/\Output。accept_connection 把
            // CD_CONNECTION_INFORMATION 写进 msg.body 并填充 msg.complete，
            // 提交交给下一轮 READ_IO。
            condrv_io::accept_connection(server, msg, condrv_input, condrv_output);
            LOG2("CONNECT accepted input=%p output=%p processCount=%zu", condrv_input.get(), condrv_output.get(),
                 processes.count());
            return;
        }
        else
        {
            // 后续 CONNECT 只代表子进程加入现有控制台。它共享首个连接的
            // \Input/\Output，因此不需要新建句柄。
            // 加入的进程仍然需要为stdin和stdout获取对象id，就像系统控制台为每个进程提供的一样。
            // 空的回复将它们留为0，因此对子进程的stdout的每次调用（从shell启动的ssh.exe）
            // 都被路由为输入句柄调用：
            // SetConsoleMode(stdout)失败，甚至覆盖了输入模式。重用第一个连接的id。
            //
            // 这里用 piggyback：只填 msg.complete，由 message_router 的下一轮 READ_IO
            // 携带提交，省掉一次 COMPLETE_IO 往返。
            //
            // 回复载荷必须活到那次 READ_IO，因此 prepare_connection_completion
            // 把它放在 msg.body 而不是栈变量：message_router 的双缓冲保证本缓冲在
            // completion 被提交前不会被下一条消息覆盖。CONNECT 的
            // 请求体（CONSOLE_SERVER_MSG）此时已经不再被读取，可以安全重用。
            condrv_io::prepare_connection_completion(msg, condrv_input.view(), condrv_output.view(),
                                                static_cast<ULONG_PTR>(client_pid));
            LOG2("CONNECT joined existing console processCount=%zu", processes.count());
            return;
        }
    }

    bool handle_disconnect(condrv_io::io_msg &msg)
    {
        // msg.descriptor.Process 是要从当前控制台进程列表删除的 pid；ConDrv
        // 对象句柄不随单个进程 DISCONNECT 释放。
        auto pid = static_cast<DWORD>(msg.descriptor.Process);
        processes.remove(pid);
        LOG2("DISCONNECT pid=%lu processCount=%zu", pid, processes.count());

        // DISCONNECT 没有输出载荷，成功 completion 足以让 ConDrv 继续派发消息。
        condrv_io::prepare_completion(msg);
        return true;
    }

    bool handle_create_object(condrv_io::io_msg &msg)
    {
        // CREATE_OBJECT body 是 CD_CREATE_OBJECT_INFORMATION；它描述客户端
        // 想打开当前 Input/Output 还是新 Output，以及访问掩码。
        if (msg.descriptor.InputSize < sizeof(CD_CREATE_OBJECT_INFORMATION))
        {
            LOG2("CREATE_OBJECT short input=%lu", msg.descriptor.InputSize);
            condrv_io::prepare_completion(msg, status_invalid_parameter);
            return true;
        }

        // CREATE_OBJECT 的输入体由 ConDrv 提供。GENERIC 类型需要根据访问
        // 掩码推导为当前 input 或 output；无法推导的类型返回失败。
        auto *req = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);

        // ObjectType 可能已经是具体对象，也可能是 GENERIC。GENERIC 只在访问
        // 掩码能唯一指向读或写时才转换。
        auto type = req->ObjectType;
        if (type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }
        // nh 在成功时 release 给 completion；release 后本进程不再关闭该句柄。
        // input_id/output_id 保存 release 前的句柄值，用于 CLOSE_OBJECT 识别
        // 客户端关闭的是哪个逻辑对象。
        win32::handle nh;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            nh = condrv::create_client_handle(server, L"\\Input");

            // 缓存返回给客户端的句柄值。CLOSE_OBJECT 只用它清理本地 id 状态。
            input_id = reinterpret_cast<ULONG_PTR>(nh.get());
            LOG2("CREATE_OBJECT input handle=%p access=0x%08lx", nh.get(), req->DesiredAccess);
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
            nh = condrv::create_client_handle(server, L"\\Output");
            output_id = reinterpret_cast<ULONG_PTR>(nh.get());
            LOG2("CREATE_OBJECT output handle=%p access=0x%08lx", nh.get(), req->DesiredAccess);
            break;
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            nh = condrv::create_client_handle(server, L"\\Output");
            alternate_output_id = reinterpret_cast<ULONG_PTR>(nh.get());
            LOG2("CREATE_OBJECT alternate output handle=%p access=0x%08lx", nh.get(), req->DesiredAccess);
            break;
        default:
            LOG2("CREATE_OBJECT unsupported type=%lu access=0x%08lx", req->ObjectType, req->DesiredAccess);
            condrv_io::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
        condrv_io::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(nh.release()));
        return true;
    }

    bool handle_close_object(condrv_io::io_msg &msg)
    {
        // msg.descriptor.Object 是客户端传回的句柄值，不一定仍然对应一个
        // 当前对象；不匹配时也按成功完成，避免关闭未知句柄阻塞客户端。
        auto id = msg.descriptor.Object;
        if (id == input_id)
            input_id = 0;
        if (id == output_id)
            output_id = 0;
        if (id == alternate_output_id)
            alternate_output_id = 0;
        LOG2("CLOSE_OBJECT object=%llu inputId=%llu outputId=%llu altOutputId=%llu", id, input_id, output_id,
             alternate_output_id);
        condrv_io::prepare_completion(msg);
        return true;
    }

    object_kind kind_from_object(ULONG_PTR id) const noexcept
    {
        // id 是 API 消息 descriptor.Object。api_router 用它判断同一个 API
        // 请求应按 input handle 还是 output handle 的模式位处理。
        if (id != 0 && id == input_id)
            return object_kind::input;
        if (id != 0 && id == output_id)
            return object_kind::output;
        if (id != 0 && id == alternate_output_id)
            return object_kind::alternate_output;
        // 第一个 CONNECT 的对象（accept_connection）是每个附加进程继承的 stdin/stdout，它们的 id
        // 就是句柄值本身。没有它，SetConsoleMode 在 stdout 上被当作输入模式请求而拒绝，因此 ssh.exe 无法设置
        // DISABLE_NEWLINE_AUTO_RETURN，每个裸 LF 也会把光标返回到列 0。
        if (id != 0 && id == reinterpret_cast<ULONG_PTR>(condrv_input.get()))
            return object_kind::input;
        if (id != 0 && id == reinterpret_cast<ULONG_PTR>(condrv_output.get()))
            return object_kind::output;
        return object_kind::unknown;
    }

    // 完成 RAW_FLUSH 对象消息。input_buffer 的清空由 message_router 负责，
    // 这里只准备 ConDrv completion。
    void handle_raw_flush(condrv_io::io_msg &msg) noexcept
    {
        // RAW_FLUSH 本身没有额外载荷；input_buffer 已由 message_router 清空。
        condrv_io::prepare_completion(msg);
    }
};

} // namespace corehost::conpty