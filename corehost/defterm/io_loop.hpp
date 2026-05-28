// ── defterm/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环 — defterm 专用
//
// 提供可复用的消息循环骨架，由 defterm 的 connect_handler 使用。
// 调用方提供 Handler 对象，通过模板参数定制 CONNECT 和非 CONNECT
// 消息的处理方式。
//
// Handler 需提供:
//   bool on_connect(miniio::io_msg &msg, connect_completion &completion)
//                                           — CONNECT 处理, 并说明 completion
//                                             是否已由 accept_connection 显式提交
//   bool on_message(miniio::io_msg &msg)  — 非 CONNECT 消息, true=已完成, false=挂起
//   void on_idle()                         — 空闲时调用, 服务挂起消息
//   bool has_pending() const               — 检查是否有挂起 I/O
//   bool should_exit() const               — VT pipe 断开后 I/O 循环退出

#pragma once
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"

namespace defterm
{

enum class connect_completion
{
    explicit_complete,
    inline_complete,
};

[[nodiscard]] inline const wchar_t *io_function_name(ULONG function) noexcept
{
    switch (function)
    {
    case 0:
        return L"None";
    case CONSOLE_IO_CONNECT:
        return L"CONNECT";
    case CONSOLE_IO_DISCONNECT:
        return L"DISCONNECT";
    case CONSOLE_IO_CREATE_OBJECT:
        return L"CREATE_OBJECT";
    case CONSOLE_IO_CLOSE_OBJECT:
        return L"CLOSE_OBJECT";
    case CONSOLE_IO_RAW_WRITE:
        return L"RAW_WRITE";
    case CONSOLE_IO_RAW_READ:
        return L"RAW_READ";
    case CONSOLE_IO_USER_DEFINED:
        return L"USER_DEFINED";
    case CONSOLE_IO_RAW_FLUSH:
        return L"RAW_FLUSH";
    default:
        return L"UNKNOWN";
    }
}

// ── run_io_loop ────────────────────────────────────────────
// 对应原版 ConsoleCreateIoThread 后半段 + ConsoleIoThread 的消息循环。
//
// 原版用两个不同的 CONSOLE_API_MSG (ReceiveMsg + ReplyMsg):
//   ReadIo(ReplyMsg, &ReceiveMsg)
//   → DeviceIoControl(IOCTL_READ_IO, ReplyMsg, ..., &ReceiveMsg, ...)
//   input (ReplyMsg) 和 output (ReceiveMsg) 是不同对象。
//
// ★ 关键: 不能像之前那样 prev=&msg.complete 同时 output=&msg.descriptor,
//   因为 prev->Write.Data 指向 msg.body，而新消息也写入 msg.body，
//   驱动可能先写后读导致完成数据被覆盖。
//
// 此处用双缓冲: msgA/msgB 交替，保证 input 和 output 永不重叠。
template <typename Handler>
inline void run_io_loop(win32::handle_view server, win32::handle_view ev, Handler &handler)
{
    LOG("run_io_loop: enter server=%p event=%p", server.get(), ev.get());
    miniio::set_server_info(server, ev);

    miniio::io_msg msgA{}, msgB{};
    miniio::io_msg *cur = &msgA;
    miniio::io_msg *prev_done = nullptr;
    unsigned long long iteration = 0;

    for (;;)
    {
        ++iteration;
        // 有上一条完成包待提交时，必须立即进入 read_io(prev_comp, ...)。
        // 若在提交 completion 前先等待，会把每个 Console API 往返都人为延迟。
        //
        // 另外，终端键盘数据走 vt_in 管道，不会触发 ConDrv 的 ev。
        // 因此空闲时必须先 on_idle() 扫 vt_in，再短等待 ConDrv 事件；否则键入只能被
        // 16ms 轮询发现，PowerShell/PSReadLine 多轮小 API 会把延迟放大成明显卡顿。
        if (!handler.has_pending() && prev_done == nullptr)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                ::WaitForSingleObject(ev.get(), 1);
        }

        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
        if (prev_comp)
        {
            LOG("run_io_loop: read #%llu submitting completion id=%08lx:%08lx status=0x%08lx info=%llu", iteration,
                prev_comp->Identifier.HighPart, prev_comp->Identifier.LowPart,
                static_cast<unsigned long>(prev_comp->IoStatus.Status),
                static_cast<unsigned long long>(prev_comp->IoStatus.Information));
        }
        else
        {
            LOG("run_io_loop: read #%llu without completion", iteration);
        }

        auto read_result = miniio::read_io_try(server, prev_comp, *cur);
        if (read_result == miniio::read_io_result::disconnected)
        {
            LOG("run_io_loop: disconnected after read #%llu", iteration);
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            // 和原版 ConDrvDeviceComm::ReadIo 一致：completion 已经作为
            // IOCTL_READ_IO 的输入交给驱动，ERROR_IO_PENDING 只表示没有新
            // 消息可取，不能在下一轮重复提交同一个 completion。
            if (prev_done)
                LOG("run_io_loop: read #%llu pending; completion consumed by READ_IO", iteration);
            prev_done = nullptr;
            handler.on_idle();
            if (handler.should_exit())
            {
                LOG("run_io_loop: handler requested exit while pending");
                break;
            }
            continue;
        }
        prev_done = nullptr;

        LOG("run_io_loop: got #%llu func=%ls(%lu) id=%08lx:%08lx pid=%llu object=%llu in=%lu out=%lu", iteration,
            io_function_name(cur->descriptor.Function), cur->descriptor.Function, cur->descriptor.Identifier.HighPart,
            cur->descriptor.Identifier.LowPart, static_cast<unsigned long long>(cur->descriptor.Process),
            static_cast<unsigned long long>(cur->descriptor.Object), cur->descriptor.InputSize,
            cur->descriptor.OutputSize);

        if (cur->descriptor.Function == 0)
        {
            handler.on_idle();
            if (handler.should_exit())
            {
                LOG("run_io_loop: handler requested exit on empty message");
                break;
            }
            continue;
        }

        if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
        {
            if (handler.on_message(*cur))
            {
                prev_done = cur;
                LOG("run_io_loop: message completed inline func=%ls(%lu) id=%08lx:%08lx",
                    io_function_name(cur->descriptor.Function), cur->descriptor.Function,
                    cur->descriptor.Identifier.HighPart, cur->descriptor.Identifier.LowPart);
                handler.on_idle();
                if (handler.should_exit())
                {
                    LOG("run_io_loop: handler requested exit after inline completion");
                    break;
                }
            }
            else
            {
                LOG("run_io_loop: message pending func=%ls(%lu)", io_function_name(cur->descriptor.Function),
                    cur->descriptor.Function);
                while (handler.has_pending())
                {
                    handler.on_idle();
                    if (handler.has_pending())
                        ::WaitForSingleObject(ev.get(), 16);
                }
                LOG("run_io_loop: pending message completed");
                if (handler.should_exit())
                {
                    LOG("run_io_loop: handler requested exit after pending completion");
                    break;
                }
            }
            cur = (cur == &msgA) ? &msgB : &msgA;
            continue;
        }

        LOG("run_io_loop: dispatch CONNECT id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
            cur->descriptor.Identifier.LowPart);
        connect_completion connect_result = connect_completion::explicit_complete;
        if (!handler.on_connect(*cur, connect_result))
        {
            LOG("run_io_loop: CONNECT handler requested loop exit");
            return;
        }

        if (connect_result == connect_completion::inline_complete)
        {
            LOG("run_io_loop: CONNECT completed inline id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
                cur->descriptor.Identifier.LowPart);
            prev_done = cur;
        }
        else
        {
            // 首次 CONNECT 由 miniio::accept_connection() 立即通过
            // IOCTL_COMPLETE_IO 单独完成。这里必须清空 prev_done，避免
            // 下一次 IOCTL_READ_IO 再提交一次已经完成的 CONNECT completion。
            // 否则 completion 的 Write.Data 会指向已经失效的栈上
            // CD_CONNECTION_INFORMATION。
            LOG("run_io_loop: CONNECT completed explicitly id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
                cur->descriptor.Identifier.LowPart);
            prev_done = nullptr;
        }
        cur = (cur == &msgA) ? &msgB : &msgA;
    }

    LOG("run_io_loop: exit");
}

// ── 非 CONNECT 消息分派（mini console 模式） ─────────────
//
// 提供最小化控制台所需的 I/O 响应，使客户端进程能正常运行
// 到结束而非卡死在 ReadConsole/WriteConsole。
//
//   DISCONNECT    — 客户端退出，释放 I/O 句柄
//   CREATE_OBJECT — 创建 \Input / \Output 句柄
//   CLOSE_OBJECT  — 关闭句柄，直接确认
//   RAW_WRITE     — 丢弃数据但确认（防止客户端阻塞）
//   RAW_READ      — 返回 0 字节 EOF
//   USER_DEFINED  — 不支持，返回 STATUS_UNSUCCESSFUL
//   RAW_FLUSH     — 直接确认
inline void dispatch_non_connect(win32::handle_view server, miniio::io_msg &msg, win32::handle &input,
                                 win32::handle &output)
{
    LOG("dispatch_non_connect: func=%ls(%lu) id=%08lx:%08lx pid=%llu object=%llu inputHandle=%p outputHandle=%p",
        io_function_name(msg.descriptor.Function), msg.descriptor.Function, msg.descriptor.Identifier.HighPart,
        msg.descriptor.Identifier.LowPart, static_cast<unsigned long long>(msg.descriptor.Process),
        static_cast<unsigned long long>(msg.descriptor.Object), input.get(), output.get());

    switch (msg.descriptor.Function)
    {
    case 0:
        break;
    case CONSOLE_IO_CONNECT:
        break;
    case CONSOLE_IO_DISCONNECT:
        LOG("dispatch_non_connect: disconnect clears handles input=%p output=%p", input.get(), output.get());
        input.clear();
        output.clear();
        miniio::prepare_completion(msg);
        break;

    case CONSOLE_IO_CREATE_OBJECT: {
        auto *req = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
        auto type = req->ObjectType;
        if (type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }

        win32::handle new_handle;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            new_handle = condrv::create_client_handle(server, L"\\Input");
            LOG("dispatch_non_connect: created input object handle=%p", new_handle.get());
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            new_handle = condrv::create_client_handle(server, L"\\Output");
            LOG("dispatch_non_connect: created output object handle=%p", new_handle.get());
            break;
        default:
            LOG("dispatch_non_connect: unsupported object type=%lu access=0x%08lx", req->ObjectType,
                req->DesiredAccess);
            miniio::prepare_completion(msg, 0xC0000001 /*STATUS_UNSUCCESSFUL*/);
            return;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(new_handle.release()));
        break;
    }

    case CONSOLE_IO_CLOSE_OBJECT:
        miniio::prepare_completion(msg);
        break;

    case CONSOLE_IO_RAW_WRITE:
        LOG("dispatch_non_connect: raw write bytes=%lu", msg.descriptor.InputSize);
        miniio::prepare_completion(msg, 0, msg.descriptor.InputSize);
        break;

    case CONSOLE_IO_RAW_READ:
        LOG("dispatch_non_connect: raw read returns EOF");
        miniio::prepare_completion(msg);
        break;

    case CONSOLE_IO_USER_DEFINED:
        LOG("dispatch_non_connect: user defined unsupported");
        miniio::prepare_completion(msg, 0xC0000001 /*STATUS_UNSUCCESSFUL*/);
        break;

    case CONSOLE_IO_RAW_FLUSH:
        miniio::prepare_completion(msg);
        break;

    default:
        std::unreachable();
    }
}

} // namespace defterm
