include_guard(GLOBAL)

# Get the lowercase normalized target arch, where x86_64, amd64, and x64 are
# unified as x86_64, aarch64 and arm64 are unified as aarch64, and others
# remain unchanged.
# It is "x86_64" on Linux, "AMD64" or "x64" on Windows, and "amd64" on
# FreeBSD/other BSDs. A case-sensitive match missed the BSD spelling.
function(normzalied_target_arch OUT_VAR)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _ARCH_LOWER)
    
    if(_ARCH_LOWER STREQUAL "x86_64" OR _ARCH_LOWER STREQUAL "amd64" OR _ARCH_LOWER STREQUAL "x64")
        set(_ARCH_NORMALIZED "x86_64")

    elseif(_ARCH_LOWER STREQUAL "arm64" OR _ARCH_LOWER STREQUAL "aarch64")
        set(_ARCH_NORMALIZED "aarch64")

    else()
        set(_ARCH_NORMALIZED "${_ARCH_LOWER}")
    endif()

    set(${OUT_VAR} "${_ARCH_NORMALIZED}" PARENT_SCOPE)
endfunction()
