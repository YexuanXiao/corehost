#pragma once
#include <windows.h>
#include "miniio/io_thread.hpp"

namespace conpty
{

enum class PendingKind
{
    None,
    RawRead,
    ConsoleRead,
    ConsoleInput
};

class pending_io_state
{
  public:
    PendingKind kind() const noexcept
    {
        return _kind;
    }

    bool has_pending() const noexcept
    {
        return _kind != PendingKind::None;
    }

    void begin_raw_read(miniio::io_msg &msg, bool process_control_z) noexcept
    {
        _kind = PendingKind::RawRead;
        _raw_read = &msg;
        _console_read = nullptr;
        _console_input = nullptr;
        _unicode = false;
        _process_control_z = process_control_z;
    }

    void begin_console_read(miniio::io_msg &msg, bool unicode, bool process_control_z) noexcept
    {
        _kind = PendingKind::ConsoleRead;
        _raw_read = nullptr;
        _console_read = &msg;
        _console_input = nullptr;
        _unicode = unicode;
        _process_control_z = process_control_z;
    }

    void begin_console_read_mode(bool unicode, bool process_control_z) noexcept
    {
        _kind = PendingKind::ConsoleRead;
        _raw_read = nullptr;
        _console_read = nullptr;
        _console_input = nullptr;
        _unicode = unicode;
        _process_control_z = process_control_z;
    }

    void begin_console_input(miniio::io_msg &msg) noexcept
    {
        _kind = PendingKind::ConsoleInput;
        _raw_read = nullptr;
        _console_read = nullptr;
        _console_input = &msg;
    }

    miniio::io_msg *raw_read() const noexcept
    {
        return _raw_read;
    }

    miniio::io_msg *console_read() const noexcept
    {
        return _console_read;
    }

    miniio::io_msg *console_input() const noexcept
    {
        return _console_input;
    }

    bool unicode() const noexcept
    {
        return _unicode;
    }

    bool process_control_z() const noexcept
    {
        return _process_control_z;
    }

    bool vt_eof() const noexcept
    {
        return _vt_eof;
    }

    void set_vt_eof(bool eof) noexcept
    {
        _vt_eof = eof;
    }

    void clear() noexcept
    {
        _kind = PendingKind::None;
        _raw_read = nullptr;
        _console_read = nullptr;
        _console_input = nullptr;
        _unicode = false;
    }

  private:
    PendingKind _kind = PendingKind::None;
    miniio::io_msg *_raw_read = nullptr;
    miniio::io_msg *_console_read = nullptr;
    miniio::io_msg *_console_input = nullptr;
    bool _unicode = false;
    bool _vt_eof = false;
    bool _process_control_z = false;
};

} // namespace conpty
