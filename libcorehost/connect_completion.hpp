// ── conpty/connect_completion.hpp ─────────────────────────
// CONNECT 的完成方式。accept_connection 会显式 CompleteIo；后续
// prepare_completion 则需要由下一轮 READ_IO 提交 completion。

#pragma once

namespace conpty
{

enum class connect_completion
{
    explicit_complete,
    inline_complete,
};

} // namespace conpty
