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

namespace conpty
{

struct io_state
{
    enum class object_kind
    {
        unknown,
        input,
        output,
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

    void set_server(win32::handle_view s) noexcept
    {
        server = s;
    }

    // 最近一次 CONNECT 的客户端 pid。0 表示尚未收到 CONNECT。
    ULONG_PTR client_pid = 0;

    // GetConsoleProcessList 兼容数据。数组保存当前已连接进程 pid；
    // 超过容量的新 pid 会被忽略，以避免写越界。
    static constexpr size_t max_processes = 64;
    std::array<DWORD, max_processes> process_list{};
    size_t process_count = 0;

    void add_process(DWORD pid)
    {
        const auto end = process_list.begin() + static_cast<std::ptrdiff_t>(process_count);
        if (std::find(process_list.begin(), end, pid) != end)
            return;
        if (process_count < max_processes)
            process_list[process_count++] = pid;
    }

    void remove_process(DWORD pid)
    {
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
        // descriptor.Process 是 ConDrv 记录的客户端 pid。它必须能放入 DWORD，
        // 因为 Win32 进程列表 API 以 DWORD 表示 pid。
        client_pid = msg.descriptor.Process;

        // 同一 pid 可能重复 CONNECT；add_process 会去重，避免
        // GetConsoleProcessList 返回重复项。
        add_process(static_cast<DWORD>(client_pid));
        if (!condrv_input.valid())
        {
            // 首个 CONNECT 需要创建 \Input/\Output，并由 accept_connection
            // 直接调用 COMPLETE_IO。调用方下一轮不能再次提交这个 completion。
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            input_id = reinterpret_cast<ULONG_PTR>(condrv_input.get());
            output_id = reinterpret_cast<ULONG_PTR>(condrv_output.get());
            completion = connect_completion::explicit_complete;
        }
        else
        {
            // 后续 CONNECT 只代表子进程加入现有控制台。它共享首个连接的
            // \Input/\Output，因此只填 completion，交给下一轮 READ_IO 提交。
            miniio::prepare_completion(msg);
            completion = connect_completion::inline_complete;
        }
        return true;
    }

    bool handle_disconnect(miniio::io_msg &msg)
    {
        // DISCONNECT 只移除对应 pid。condrv_input/output 属于整个会话；
        // 子进程退出时关闭它们会让仍存活的父进程失去控制台连接。
        auto pid = static_cast<DWORD>(msg.descriptor.Process);
        remove_process(pid);

        // DISCONNECT 没有输出载荷，成功 completion 足以让 ConDrv 继续派发消息。
        miniio::prepare_completion(msg);
        return true;
    }

    bool handle_create_object(miniio::io_msg &msg)
    {
        if (msg.descriptor.InputSize < sizeof(CD_CREATE_OBJECT_INFORMATION))
        {
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
        win32::handle nh;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            nh = condrv::create_client_handle(server, L"\\Input");

            // 缓存返回给客户端的句柄值。CLOSE_OBJECT 只用它清理本地 id 状态。
            input_id = reinterpret_cast<ULONG_PTR>(nh.get());
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            nh = condrv::create_client_handle(server, L"\\Output");
            output_id = reinterpret_cast<ULONG_PTR>(nh.get());
            break;
        default:
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
            return true;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(nh.release()));
        return true;
    }

    bool handle_close_object(miniio::io_msg &msg)
    {
        // Object 是客户端传回的句柄值。只清除匹配的缓存 id，不关闭
        // condrv_input/condrv_output 本体，因为它们由会话生命周期管理。
        auto id = msg.descriptor.Object;
        if (id == input_id)
            input_id = 0;
        if (id == output_id)
            output_id = 0;
        miniio::prepare_completion(msg);
        return true;
    }

    object_kind kind_from_object(ULONG_PTR id) const noexcept
    {
        if (id != 0 && id == input_id)
            return object_kind::input;
        if (id != 0 && id == output_id)
            return object_kind::output;
        return object_kind::unknown;
    }

    void handle_raw_flush(miniio::io_msg &msg)
    {
        miniio::prepare_completion(msg);
    }
};

} // namespace conpty
