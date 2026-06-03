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
#include <algorithm>
#include <array>
#include <cstddef>
#include "connect_completion.hpp"
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "ntapi/condrv.hpp"
#include "utility/log.hpp"

namespace conpty
{

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

    // GetConsoleProcessList 兼容数据。数组保存当前已连接进程 pid；
    // 超过容量的新 pid 会被忽略，以避免写越界。
    static constexpr size_t max_processes = 64;
    std::array<DWORD, max_processes> process_list{};
    size_t process_count = 0;

    // 将 pid 加入当前控制台进程列表。重复 pid 不改变列表；超过容量时忽略
    // 新 pid，避免破坏已有 GetConsoleProcessList 快照。
    void add_process(DWORD pid)
    {
        // pid 来自 CONNECT descriptor.Process 或 defterm 预附加进程；同一
        // 进程重复 CONNECT 时只保留一份，匹配 GetConsoleProcessList 语义。
        const auto end = process_list.begin() + static_cast<std::ptrdiff_t>(process_count);
        if (std::find(process_list.begin(), end, pid) != end)
            return;
        if (process_count < max_processes)
            process_list[process_count++] = pid;
    }

    // 从当前控制台进程列表删除 pid。找不到 pid 时不改变列表；删除后数组
    // 前 process_count 项保持紧凑。
    void remove_process(DWORD pid)
    {
        // pid 来自 DISCONNECT descriptor.Process。数组保持紧凑，便于直接
        // 复制给 pipe_bridge 的进程快照。
        const auto end = process_list.begin() + static_cast<std::ptrdiff_t>(process_count);
        const auto it = std::find(process_list.begin(), end, pid);
        if (it == end)
            return;
        std::move(it + 1, end, it);
        --process_count;
        process_list[process_count] = 0;
    }

    bool handle_connect(miniio::io_msg &msg, connect_completion &completion)
    {
        // msg 的 body 对 CONNECT 不由本层解析；accept_connection 会使用
        // descriptor.Identifier 完成首个连接并创建 input/output 句柄。
        // completion 告诉 io_loop 是否还需要把 msg.complete 作为下一轮 READ_IO
        // 的 completion 参数提交。
        // descriptor.Process 是 ConDrv 记录的客户端 pid。它必须能放入 DWORD，
        // 因为 Win32 进程列表 API 以 DWORD 表示 pid。
        client_pid = msg.descriptor.Process;
        const bool first_connect = !condrv_input.valid();
        // first_connect 决定是否需要真正 accept \Input/\Output；后续 CONNECT
        // 只增加进程列表，不替换已有控制台对象句柄。
        LOG2("handle_connect pid=%llu first=%d processCount=%zu", client_pid, first_connect, process_count);

        // 同一 pid 可能重复 CONNECT；add_process 会去重，避免
        // GetConsoleProcessList 返回重复项。
        add_process(static_cast<DWORD>(client_pid));
        if (first_connect)
        {
            // 首个 CONNECT 需要创建 \Input/\Output，并由 accept_connection
            // 直接调用 COMPLETE_IO。调用方下一轮不能再次提交这个 completion。
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            input_id = reinterpret_cast<ULONG_PTR>(condrv_input.get());
            output_id = reinterpret_cast<ULONG_PTR>(condrv_output.get());
            completion = connect_completion::explicit_complete;
            LOG2("CONNECT accepted input=%p output=%p processCount=%zu", condrv_input.get(), condrv_output.get(),
                 process_count);
        }
        else
        {
            // 后续 CONNECT 只代表子进程加入现有控制台。它共享首个连接的
            // \Input/\Output，因此只填 completion，交给下一轮 READ_IO 提交。
            miniio::prepare_completion(msg);
            completion = connect_completion::inline_complete;
            LOG2("CONNECT joined existing console processCount=%zu", process_count);
        }
        return true;
    }

    bool handle_disconnect(miniio::io_msg &msg)
    {
        // msg.descriptor.Process 是要从当前控制台进程列表删除的 pid；ConDrv
        // 对象句柄不随单个进程 DISCONNECT 释放。
        auto pid = static_cast<DWORD>(msg.descriptor.Process);
        remove_process(pid);
        LOG2("DISCONNECT pid=%lu processCount=%zu", pid, process_count);

        // DISCONNECT 没有输出载荷，成功 completion 足以让 ConDrv 继续派发消息。
        miniio::prepare_completion(msg);
        return true;
    }

    bool handle_create_object(miniio::io_msg &msg)
    {
        // CREATE_OBJECT body 是 CD_CREATE_OBJECT_INFORMATION；它描述客户端
        // 想打开当前 Input/Output 还是新 Output，以及访问掩码。
        if (msg.descriptor.InputSize < sizeof(CD_CREATE_OBJECT_INFORMATION))
        {
            LOG2("CREATE_OBJECT short input=%lu", msg.descriptor.InputSize);
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
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
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
            return true;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(nh.release()));
        return true;
    }

    bool handle_close_object(miniio::io_msg &msg)
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
        miniio::prepare_completion(msg);
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
        return object_kind::unknown;
    }

    // 完成 RAW_FLUSH 对象消息。input_buffer 的清空由 message_router 负责，
    // 这里只准备 ConDrv completion。
    void handle_raw_flush(miniio::io_msg &msg)
    {
        // RAW_FLUSH 本身没有额外载荷；input_buffer 已由 message_router 清空。
        miniio::prepare_completion(msg);
    }
};

} // namespace conpty
