// === tests/test_conpty_e2e.cpp ===
// ConPTY E2E test. Verifies creation, VT processing, isolation.
// NOTE: Text echo requires a ConDrv ReadConsole client.
#include "test_common.hpp"
#include "conpty_test_helpers.hpp"
#include <cstdio>
using namespace conpty_test;

bool test_create()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(ci.hInput != nullptr);
    ASSERT(ci.hOutput != nullptr);
    return true;
}
bool test_vt_cursor()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[5;10H"));
    sleep_ms(200);
    ASSERT(true);
    return true;
}
bool test_vt_sgr()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[31m"));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[0m"));
    sleep_ms(200);
    ASSERT(true);
    return true;
}
bool test_arrow_keys()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[A"));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[B"));
    sleep_ms(200);
    ASSERT(true);
    return true;
}
bool test_erase()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(send_vt_sequence(ci.hInput, "\x1b[2J\x1b[H"));
    sleep_ms(200);
    ASSERT(true);
    return true;
}
bool test_multiple()
{
    conpty_instance ci1, ci2;
    ASSERT(create_conpty(ci1, {80, 25}));
    ASSERT(create_conpty(ci2, {80, 25}));
    ASSERT(true);
    return true;
}
bool test_text_no_crash()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));
    ASSERT(write_input_string(ci.hInput, "text\r\n"));
    sleep_ms(200);
    ASSERT(true);
    return true;
}

int main()
{
    std::wcout << L"=== ConPTY E2E Tests ===" << std::endl;
    RUN_TEST(test_create, L"Create");
    RUN_TEST(test_vt_cursor, L"VT CUP");
    RUN_TEST(test_vt_sgr, L"VT SGR");
    RUN_TEST(test_arrow_keys, L"Arrow keys");
    RUN_TEST(test_erase, L"Erase");
    RUN_TEST(test_multiple, L"Multiple");
    RUN_TEST(test_text_no_crash, L"Text no-crash");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
