// DLL entry point for conpty.dll.
// ConPTY 功能实现在 conpty_static.lib 中。
// 此 DLL 仅通过 conpty.def 重新导出符号。

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ulReasonForCall, LPVOID /*lpReserved*/)
{
    switch (ulReasonForCall)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
