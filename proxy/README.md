This folder contains the proxy/stub DLL (`conhost_re_proxy.dll`) used to marshal `[system_handle]` parameters for out-of-proc COM calls.

The proxy/stub DLL is built from **pre-generated** MIDL outputs checked into:
- `new/proxy/x64/` (x64 proxy/stub sources)
- `new/proxy/arm64/` (arm64 proxy/stub sources)

This keeps the build free of a `midl.exe` dependency while preserving the exact ABI and marshalling format.

midl.exe /W1 /nologo /char signed /env x64 /out "x64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env x64 /out "x64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl

midl.exe /W1 /nologo /char signed /env arm64 /out "arm64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env arm64 /out "arm64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl



