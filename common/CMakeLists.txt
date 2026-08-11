# ═══════════════════════════════════════════════════════════════
# common — 核心基础设施静态库
#
# 提供 OS 头文件 (os), NT API 封装 (ntapi), Win32 RAII (win32),
# COM 工具 (com), 工具函数 (utility), Shell 检测 (shell).
# ═══════════════════════════════════════════════════════════════

add_library(common STATIC
    utility/diagnostic_report.cpp
    utility/log.cpp
    utility/notification.cpp
    utility/temp_path.cpp
)

target_sources(common
    PUBLIC
        FILE_SET HEADERS
            FILES
                com/bstring.hpp
                com/clsid.hpp
                com/com_base.hpp
                com/com_ptr.hpp
                com/register.hpp
                ntapi/condrv.hpp
                ntapi/conwinuserrefs.h
                os/Console/ConIoSrv.h
                os/Console/conapi.h
                os/Console/condrv.h
                os/Console/conmsgl1.h
                os/Console/conmsgl2.h
                os/Console/conmsgl3.h
                os/Console/csrmsg.h
                os/Console/ntcon.h
                os/Console/ntcsrdll.h
                os/Console/ntcsrmsg.h
                os/Console/ntlpcapi.h
                os/Console/winconp.h
                os/NT/ntioapi_x.h
                os/telemetry/ProjectTelemetry.h
                os/Win32K/winuserp.h
                shell/shell.hpp
                utility/assert.hpp
                utility/cast.hpp
                utility/crtdbg.hpp
                utility/diagnostic_report.hpp
                utility/env.hpp
                utility/log.hpp
                utility/notification.hpp
                utility/temp_path.hpp
                utility/utf.hpp
                utility/wil.hpp
                win32/command_line.hpp
                win32/com_apartment.hpp
                win32/debugging.hpp
                win32/env.hpp
                win32/error.hpp
                win32/event.hpp
                win32/file.hpp
                win32/handle.hpp
                win32/hcon.hpp
                win32/hresult.hpp
                win32/io.hpp
                win32/process.hpp
                win32/process_information.hpp
                win32/registry_key.hpp
                win32/string.hpp
                win32/tags.hpp
                win32/thread.hpp
                win32/wait.hpp
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/utf_view/include/beman
    ${CMAKE_SOURCE_DIR}/proxy/${CONHOST_PROXY_ARCH_DIR}
)

target_link_libraries(common PUBLIC
    ntdll.lib
    ole32.lib
    psapi.lib
    runtimeobject.lib
    shell32.lib
)
