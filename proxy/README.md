This folder contains the proxy/stub DLL (`OpenConsoleProxy.dll`) used to marshal out-of-proc COM calls.

The proxy/stub DLL is built from **pre-generated** MIDL outputs checked into:
- `proxy/x64/` (x64 proxy/stub sources)
- `proxy/arm64/` (arm64 proxy/stub sources)

This keeps the build free of a `midl.exe` dependency while preserving the exact ABI and marshalling format.

midl.exe /W1 /nologo /char signed /env x64 /out "x64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env x64 /out "x64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl

midl.exe /W1 /nologo /char signed /env arm64 /out "arm64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env arm64 /out "arm64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl

Be cautious about registering the raw OpenConsoleProxy.dll directly to the system, because doing it multiple times can cause conflicts. The Windows Terminal MSIX package already includes its own version of OpenConsoleProxy.dll, and each distribution channel comes with its own copy as well. That said, the MSIX framework might have a way to handle situations where multiple OpenConsoleProxy.dll files are present.

If only generating header files, use the following command:

midl.exe /W1 /nologo /char signed /env x64 /iid "nul" /proxy "nul" /dlldata "nul" /out "x64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env x64 /iid "nul" /proxy "nul" /dlldata "nul" /out "x64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl

midl.exe /W1 /nologo /char signed /env arm64 /iid "nul" /proxy "nul" /dlldata "nul" /out "arm64" /h "IConsoleHandoff.h" /target "NT100" IConsoleHandoff.idl
midl.exe /W1 /nologo /char signed /env arm64 /iid "nul" /proxy "nul" /dlldata "nul" /out "arm64" /h "ITerminalHandoff.h" /target "NT100" ITerminalHandoff.idl
