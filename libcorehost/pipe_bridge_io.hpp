// ── conpty/pipe_bridge_io.hpp ─────────────────────────────
// ConDrv 传输辅助：COMPLETE_IO、READ_INPUT、server 状态查询。
//
// VT 输入不再经过本类：vt_in 由 vt_input_thread 通过 cancellable_read 独占读取，
// 输入字节交给 message_router 处理。
#pragma once
#include <windows.h>
#include <span>
#include "win32/handle.hpp"
#include "condrv_io.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

class pipe_bridge_io
{
  public:
    void set_server(win32::handle_view server) noexcept
    {
        _server = server;
    }

    void complete(CD_IO_COMPLETE &completion) const
    {
        condrv_io::complete_io(_server, completion);
    }

    [[nodiscard]] bool can_complete() const noexcept
    {
        return _server.valid();
    }

    void read_input(LUID identifier, ULONG offset, std::span<char8_t> destination) const
    {
        condrv_io::read_input(_server, identifier, offset, byte_span(destination));
    }

  private:
    [[nodiscard]] static std::span<BYTE> byte_span(std::span<char8_t> buffer) noexcept
    {
        return {reinterpret_cast<BYTE *>(buffer.data()), buffer.size()};
    }

    win32::handle_view _server;
};

} // namespace corehost::conpty
