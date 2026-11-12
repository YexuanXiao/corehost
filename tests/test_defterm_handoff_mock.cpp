// tests/test_defterm_handoff_mock.cpp
// defterm handoff 协议测试（替身层）：
// 不启动 WindowsTerminal、不碰注册表或 COM 注册，直接用测试进程内的
// IConsoleHandoff 替身对象驱动 perform_handoff()，验证：
//   1. 传给终端的参数（server/inputEvent/signalPipe/inbox、portable attach 字段）；
//   2. EstablishHandoff 失败时 perform_handoff 返回 false 且不挂起；
//   3. 成功后等待路径会终止（终端进程句柄已置位）。
#include "test_common.hpp"

#include "defterm/handoff_session.hpp"

#include "win32/event.hpp"
#include "win32/handle.hpp"

#include <windows.h>

namespace
{

bool valid_handle(HANDLE handle);

class mock_handoff final : public IConsoleHandoff
{
  public:
    explicit mock_handoff(HRESULT establish_result = S_OK) : _result{establish_result}
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override
    {
        if (object != nullptr)
            *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE EstablishHandoff(HANDLE server, HANDLE input_event, PCCONSOLE_PORTABLE_ATTACH_MSG message,
                                               HANDLE signal_pipe, HANDLE inbox_process, HANDLE *process) override
    {
        ++calls;
        seen_server = server;
        seen_input_event = input_event;
        seen_message = *message;

        // 句柄只在调用期间有效：perform_handoff 返回后会关掉自己的副本，
        // 因此在这里记录"当时是否有效"，而不是事后检查。
        server_valid = valid_handle(server);
        input_event_valid = valid_handle(input_event);
        signal_pipe_valid = valid_handle(signal_pipe);
        inbox_process_valid = valid_handle(inbox_process);

        if (_result != S_OK || process == nullptr)
            return _result;

        // 模拟终端进程句柄：已置位的事件，让 perform_handoff 的等待立即完成。
        *process = ::CreateEventW(nullptr, TRUE, TRUE, nullptr);
        return *process != nullptr ? S_OK : E_OUTOFMEMORY;
    }

    int calls = 0;
    HANDLE seen_server = nullptr;
    HANDLE seen_input_event = nullptr;
    CONSOLE_PORTABLE_ATTACH_MSG seen_message{};
    bool server_valid = false;
    bool input_event_valid = false;
    bool signal_pipe_valid = false;
    bool inbox_process_valid = false;

  private:
    HRESULT _result;
};

bool valid_handle(HANDLE handle)
{
    DWORD flags = 0;
    return handle != nullptr && ::GetHandleInformation(handle, &flags) != FALSE;
}

// CONNECT 描述符经 portable attach 结构传给终端，字段必须一一对应。
bool handoff_receives_console_connection()
{
    corehost::condrv_io::io_msg msg{};
    msg.descriptor.Identifier.LowPart = 0x12345678;
    msg.descriptor.Identifier.HighPart = 0x0ABCDEF0;
    msg.descriptor.Process = 4242;
    msg.descriptor.Object = 99;
    msg.descriptor.Function = CONSOLE_IO_CONNECT;
    msg.descriptor.InputSize = 64;
    msg.descriptor.OutputSize = 128;

    const auto portable = corehost::defterm::make_portable_attach_msg(msg);
    ASSERT_EQ(portable.IdLowPart, 0x12345678u);
    ASSERT_EQ(portable.IdHighPart, 0x0ABCDEF0);
    ASSERT_EQ(portable.Process, 4242u);
    ASSERT_EQ(portable.Object, 99u);
    ASSERT_EQ(portable.Function, static_cast<ULONG>(CONSOLE_IO_CONNECT));
    ASSERT_EQ(portable.InputSize, 64u);
    ASSERT_EQ(portable.OutputSize, 128u);

    mock_handoff terminal;
    auto server = win32::duplicate_self();
    win32::event input_event{win32::create_tag, /*manual_reset=*/false, /*initial=*/false};

    const bool accepted =
        corehost::defterm::perform_handoff(terminal, server.view(), input_event.view(), portable, 4242);
    ASSERT(accepted);
    ASSERT_EQ(terminal.calls, 1);

    // 参数必须真实有效，且 portable 结构原样送达。
    ASSERT(terminal.server_valid);
    ASSERT(terminal.input_event_valid);
    ASSERT(terminal.signal_pipe_valid);
    ASSERT(terminal.inbox_process_valid);
    ASSERT_EQ(terminal.seen_message.IdLowPart, 0x12345678u);
    ASSERT_EQ(terminal.seen_message.Process, 4242u);
    ASSERT_EQ(terminal.seen_message.Function, static_cast<ULONG>(CONSOLE_IO_CONNECT));
    return true;
}

// 终端拒绝（EstablishHandoff 返回失败）时，客户端必须得到 false，而不是挂起。
bool handoff_rejection_is_reported()
{
    corehost::condrv_io::io_msg msg{};
    msg.descriptor.Function = CONSOLE_IO_CONNECT;
    const auto portable = corehost::defterm::make_portable_attach_msg(msg);

    mock_handoff terminal{E_FAIL};
    auto server = win32::duplicate_self();
    win32::event input_event{win32::create_tag, false, false};

    const bool accepted = corehost::defterm::perform_handoff(terminal, server.view(), input_event.view(), portable, 1);
    ASSERT(!accepted);
    ASSERT_EQ(terminal.calls, 1);
    return true;
}

} // namespace

int main()
{
    std::wcout << L"=== Defterm Handoff (mock) ===" << std::endl;
    RUN_TEST(handoff_receives_console_connection, L"Handoff passes connection to terminal");
    RUN_TEST(handoff_rejection_is_reported, L"Handoff rejection is reported");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
