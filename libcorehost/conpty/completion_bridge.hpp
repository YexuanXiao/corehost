// ── conpty/completion_bridge.hpp ───────────────────
// Layer 2/3: ReadConsole completion 桥
//
// 与 conpty/completion_bridge.hpp 相同 — 无文本编码依赖。
#pragma once
#include <windows.h>
#include <cstring>
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"

namespace conpty
{

struct completion_bridge
{
    win32::handle_view server;
    ULONG64 pending_id = 0;
    BYTE pending_outbuf[sizeof(CONSOLE_READCONSOLE_MSG) + 8192];
    bool is_unicode = false;

    void set_server(win32::handle_view s) noexcept
    {
        server = s;
    }

    void save_request(const CONSOLE_READCONSOLE_MSG *req, ULONG64 id, bool uni)
    {
        std::memcpy(pending_outbuf, req, sizeof(CONSOLE_READCONSOLE_MSG));
        pending_id = id;
        is_unicode = uni;
    }

    void on_line_ready(const void *data, DWORD bytes)
    {
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(pending_outbuf);
        auto *db = pending_outbuf + sizeof(CONSOLE_READCONSOLE_MSG);
        auto maxd = static_cast<DWORD>(sizeof(pending_outbuf) - sizeof(CONSOLE_READCONSOLE_MSG));
        auto cp = bytes > maxd ? maxd : bytes;
        std::memcpy(db, data, cp);
        req->NumBytes = cp;
        do_complete(cp);
    }

    void on_eof()
    {
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(pending_outbuf);
        req->NumBytes = 0;
        do_complete(0);
    }

  private:
    void do_complete(ULONG data_bytes)
    {
        auto sz = static_cast<ULONG_PTR>(sizeof(CONSOLE_READCONSOLE_MSG) + data_bytes);
        CD_IO_COMPLETE comp{};
        comp.Identifier.LowPart = static_cast<ULONG>(pending_id);
        comp.Identifier.HighPart = static_cast<LONG>(pending_id >> 32);
        comp.IoStatus.Status = 0;
        comp.IoStatus.Information = sz;
        comp.Write.Data = pending_outbuf;
        comp.Write.Size = sz;
        comp.Write.Offset = 0;
        miniio::complete_io(server, comp);
    }
};

} // namespace conpty
