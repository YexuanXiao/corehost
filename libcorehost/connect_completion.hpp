// ── conpty/connect_completion.hpp ─────────────────────────
// CONNECT 的完成方式。accept_connection 会显式 CompleteIo；后续
// prepare_completion 则需要由下一轮 READ_IO 提交 completion。

#pragma once

namespace corehost::conpty
{

enum class connect_completion
{
    // accept_connection 已经对 CONNECT 请求调用 CompleteIo。调用者不能再把同一
    // 条 CONNECT completion 放进下一轮 READ_IO，否则 ConDrv 会收到重复完成。
    explicit_complete,
    // CONNECT 结果仍留在当前 msg 里，run_io_loop_no_setup 会把它作为下一次
    // READ_IO 的 piggyback completion 提交。
    inline_complete,
};

} // namespace corehost::conpty
