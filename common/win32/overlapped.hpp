#pragma once

// ── Win32 overlapped I/O 辅助 ──────────────────────────────
//
// 针对匿名管道 overlapped 读的最小封装：
//   begin_overlapped_read   发起异步读（自动复位 event 作为完成通知）
//   finish_overlapped_read  完成事件触发后取回结果
//
// 设计：
// - 同一 OVERLAPPED 同一时刻最多一个未完成读（调用方保证不复用）。
// - hEvent 使用自动复位事件：WaitForMultipleObjects 返回后事件已复位，
//   finish 用 GetOverlappedResult(FALSE) 从 OVERLAPPED 内部取结果。
// - 错误策略：管道断开（0 字节 EOF / broken pipe / 未连接）是唯一可预期的
//   "正常失败"，收敛为 closed；其余错误视为不可恢复，直接抛 win32::error。

#include <windows.h>

#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/io.hpp"

namespace win32
{

enum class overlapped_io_status
{
    pending, // 已发起，等待完成事件
    done,    // 已读取 bytes 字节
    closed,  // 管道断开（0 字节 EOF 或 broken pipe）
};

struct overlapped_io_result
{
    overlapped_io_status status{};
    DWORD bytes{};

    [[nodiscard]] bool pending() const noexcept
    {
        return status == overlapped_io_status::pending;
    }

    [[nodiscard]] bool done() const noexcept
    {
        return status == overlapped_io_status::done;
    }

    [[nodiscard]] bool closed() const noexcept
    {
        return status == overlapped_io_status::closed;
    }
};

// 发起一个 overlapped read。ov 必须已绑定事件句柄（hEvent）。
// 立即完成返回 done；异步进行返回 pending；管道断开返回 closed；
// 其余错误抛 win32::error。
[[nodiscard]] inline overlapped_io_result begin_overlapped_read(win32::handle_view pipe, void *buffer, DWORD size,
                                                                OVERLAPPED &ov)
{
    DWORD bytes_read = 0;
    if (::ReadFile(pipe.get(), buffer, size, &bytes_read, &ov))
        return {overlapped_io_status::done, bytes_read};

    const auto err = win32::get_last_error();
    if (err == win32::error::io_pending)
        return {overlapped_io_status::pending, 0};
    if (is_pipe_closed_error(err))
        return {overlapped_io_status::closed, 0};
    win32::throw_last_error(err);
}

// 完成事件触发后取回结果。bytes == 0（EOF）与 broken pipe 统一为 closed；
// 其余错误抛 win32::error。
[[nodiscard]] inline overlapped_io_result finish_overlapped_read(win32::handle_view pipe, OVERLAPPED &ov)
{
    DWORD bytes_read = 0;
    if (::GetOverlappedResult(pipe.get(), &ov, &bytes_read, FALSE))
    {
        if (bytes_read == 0)
            return {overlapped_io_status::closed, 0};
        return {overlapped_io_status::done, bytes_read};
    }

    const auto err = win32::get_last_error();
    if (is_pipe_closed_error(err))
        return {overlapped_io_status::closed, 0};
    win32::throw_last_error(err);
}

} // namespace win32
