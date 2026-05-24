# OpenConsole 数据结构与数据流全分析

> 基于 `terminal/src/` 原始代码完整分析。
> 日期: 2026-05-18
> 覆盖: 控制台 API 分层体系 (L1/L2/L3)、核心数据结构、虚拟终端管线、完整数据流向。

---

## 第一部分：控制台 API 分层体系

### 1.1 ApiNumber 编码

```
ApiNumber = Layer << 24 | Index

Layer = ApiNumber >> 24
Index = ApiNumber & 0x00FFFFFF
```

`CONSOLE_FIRST_API_NUMBER(Layer)` 宏计算 `(Layer << 24)`。

`server/ApiSorter.cpp` 中的分发逻辑:
```cpp
const auto LayerNumber = (Message->msgHeader.ApiNumber >> 24) - 1;
const auto ApiNumber = Message->msgHeader.ApiNumber & 0xffffff;
const auto& Table = ConsoleApiLayerTable[LayerNumber];
const auto& Descriptor = Table.Descriptor[ApiNumber];
Descriptor.Routine(Message);  // 调用对应处理函数
```

### 1.2 L1 API (Layer 1: 最基本控制台操作)

从 `server/ApiSorter.cpp` 提取的完整 L1 API 表 (`ConsoleApiLayer1[]`):

| 索引 | ApiNumber | API 名称 | 处理函数 | 结构体 | 大小 |
|------|-----------|---------|---------|--------|------|
| 0 | `0x01000000` | `GetConsoleCP` | `ServerGetConsoleCP` | `CONSOLE_GETCP_MSG` | 8 |
| 1 | `0x01000001` | `GetConsoleMode` | `ServerGetConsoleMode` | `CONSOLE_MODE_MSG` | 4 |
| 2 | `0x01000002` | `SetConsoleMode` | `ServerSetConsoleMode` | `CONSOLE_MODE_MSG` | 4 |
| 3 | `0x01000003` | `GetNumberOfConsoleInputEvents` | `ServerGetNumberOfInputEvents` | `CONSOLE_GETNUMBEROFINPUTEVENTS_MSG` | 4 |
| 4 | `0x01000004` | `GetConsoleInput` | `ServerGetConsoleInput` | `CONSOLE_GETCONSOLEINPUT_MSG` | 8 |
| 5 | `0x01000005` | `ReadConsole` | `ServerReadConsole` | `CONSOLE_READCONSOLE_MSG` | 24 |
| 6 | `0x01000006` | `WriteConsole` | `ServerWriteConsole` | `CONSOLE_WRITECONSOLE_MSG` | 8 |
| 7 | `0x01000007` | ~~NotifyLastClose~~ | `ServerDeprecatedApi` | — | 0 |
| 8 | `0x01000008` | `GetConsoleLangId` | `ServerGetConsoleLangId` | `CONSOLE_LANGID_MSG` | 2 |
| 9 | `0x01000009` | ~~MapBitmap~~ | `ServerDeprecatedApi` | `CONSOLE_MAPBITMAP_MSG` | 16 |

### 1.3 L2 API (Layer 2: Screen Buffer 和窗口操作)

| 索引 | ApiNumber | API 名称 | 处理函数 | 结构体 | 大小 |
|------|-----------|---------|---------|--------|------|
| 0 | `0x02000000` | `FillConsoleOutput` | `ServerFillConsoleOutput` | `CONSOLE_FILLCONSOLEOUTPUT_MSG` | 16 |
| 1 | `0x02000001` | `GenerateConsoleCtrlEvent` | `ServerGenerateConsoleCtrlEvent` | `CONSOLE_CTRLEVENT_MSG` | 8 |
| 2 | `0x02000002` | `SetConsoleActiveScreenBuffer` | `ServerSetConsoleActiveScreenBuffer` | — | 0 |
| 3 | `0x02000003` | `FlushConsoleInputBuffer` | `ServerFlushConsoleInputBuffer` | — | 0 |
| 4 | `0x02000004` | `SetConsoleCP` | `ServerSetConsoleCP` | `CONSOLE_SETCP_MSG` | 8 |
| 5 | `0x02000005` | `GetConsoleCursorInfo` | `ServerGetConsoleCursorInfo` | `CONSOLE_GETCURSORINFO_MSG` | 8 |
| 6 | `0x02000006` | `SetConsoleCursorInfo` | `ServerSetConsoleCursorInfo` | `CONSOLE_SETCURSORINFO_MSG` | 8 |
| 7 | `0x02000007` | `GetConsoleScreenBufferInfo` | `ServerGetConsoleScreenBufferInfo` | `CONSOLE_SCREENBUFFERINFO_MSG` | 88 |
| 8 | `0x02000008` | `SetConsoleScreenBufferInfo` | `ServerSetConsoleScreenBufferInfo` | `CONSOLE_SCREENBUFFERINFO_MSG` | 88 |
| 9 | `0x02000009` | `SetConsoleScreenBufferSize` | `ServerSetConsoleScreenBufferSize` | `CONSOLE_SETSCREENBUFFERSIZE_MSG` | 4 |
| 10 | `0x0200000A` | `SetConsoleCursorPosition` | `ServerSetConsoleCursorPosition` | `CONSOLE_SETCURSORPOSITION_MSG` | 4 |
| 11 | `0x0200000B` | `GetLargestConsoleWindowSize` | `ServerGetLargestConsoleWindowSize` | `CONSOLE_GETLARGESTWINDOWSIZE_MSG` | 4 |
| 12 | `0x0200000C` | `ScrollConsoleScreenBuffer` | `ServerScrollConsoleScreenBuffer` | `CONSOLE_SCROLLSCREENBUFFER_MSG` | 32 |
| 13 | `0x0200000D` | `SetConsoleTextAttribute` | `ServerSetConsoleTextAttribute` | `CONSOLE_SETTEXTATTRIBUTE_MSG` | 2 |
| 14 | `0x0200000E` | `SetConsoleWindowInfo` | `ServerSetConsoleWindowInfo` | `CONSOLE_SETWINDOWINFO_MSG` | 12 |
| 15 | `0x0200000F` | `ReadConsoleOutputString` | `ServerReadConsoleOutputString` | `CONSOLE_READCONSOLEOUTPUTSTRING_MSG` | 12 |
| 16 | `0x02000010` | `WriteConsoleInput` | `ServerWriteConsoleInput` | `CONSOLE_WRITECONSOLEINPUT_MSG` | 12 |
| 17 | `0x02000011` | `WriteConsoleOutput` | `ServerWriteConsoleOutput` | `CONSOLE_WRITECONSOLEOUTPUT_MSG` | 12 |
| 18 | `0x02000012` | `WriteConsoleOutputString` | `ServerWriteConsoleOutputString` | `CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG` | 12 |
| 19 | `0x02000013` | `ReadConsoleOutput` | `ServerReadConsoleOutput` | `CONSOLE_READCONSOLEOUTPUT_MSG` | 12 |
| 20 | `0x02000014` | `GetConsoleTitle` | `ServerGetConsoleTitle` | `CONSOLE_GETTITLE_MSG` | 8 |
| 21 | `0x02000015` | `SetConsoleTitle` | `ServerSetConsoleTitle` | `CONSOLE_SETTITLE_MSG` | 4 |

### 1.4 L3 API (Layer 3: 字体/显示/别名/历史/选择)

| 索引 | ApiNumber | API 名称 | 处理函数 | 结构体 | 大小 |
|------|-----------|---------|---------|--------|------|
| 0 | `0x03000000` | ~~GetNumberOfFonts~~ | `ServerDeprecatedApi` | `CONSOLE_GETNUMBEROFFONTS_MSG` | 4 |
| 1 | `0x03000001` | `GetNumberOfConsoleMouseButtons` | `ServerGetConsoleMouseInfo` | `CONSOLE_GETMOUSEINFO_MSG` | 4 |
| 2 | `0x03000002` | ~~GetFontInfo~~ | `ServerDeprecatedApi` | `CONSOLE_GETFONTINFO_MSG` | 8 |
| 3 | `0x03000003` | `GetConsoleFontSize` | `ServerGetConsoleFontSize` | `CONSOLE_GETFONTSIZE_MSG` | 8 |
| 4 | `0x03000004` | `GetCurrentConsoleFont` | `ServerGetConsoleCurrentFont` | `CONSOLE_CURRENTFONT_MSG` | 84 |
| 5 | `0x03000005` | ~~SetFont~~ | `ServerDeprecatedApi` | `CONSOLE_SETFONT_MSG` | 4 |
| 6 | `0x03000006` | ~~SetIcon~~ | `ServerDeprecatedApi` | `CONSOLE_SETICON_MSG` | 8 |
| 7 | `0x03000007` | ~~InvalidateBitmapRect~~ | `ServerDeprecatedApi` | `CONSOLE_INVALIDATERECT_MSG` | 8 |
| 8 | `0x03000008` | ~~VDM Operation~~ | `ServerDeprecatedApi` | `CONSOLE_VDM_MSG` | 32 |
| 9 | `0x03000009` | ~~SetCursor (old)~~ | `ServerDeprecatedApi` | `CONSOLE_SETCURSOR_MSG` | 8 |
| 10 | `0x0300000A` | ~~ShowCursor (old)~~ | `ServerDeprecatedApi` | `CONSOLE_SHOWCURSOR_MSG` | 8 |
| 11 | `0x0300000B` | ~~MenuControl~~ | `ServerDeprecatedApi` | `CONSOLE_MENUCONTROL_MSG` | 16 |
| 12 | `0x0300000C` | ~~SetPalette~~ | `ServerDeprecatedApi` | `CONSOLE_SETPALETTE_MSG` | 12 |
| 13 | `0x0300000D` | `SetConsoleDisplayMode` | `ServerSetConsoleDisplayMode` | `CONSOLE_SETDISPLAYMODE_MSG` | 8 |
| 14 | `0x0300000E` | ~~RegisterVDM~~ | `ServerDeprecatedApi` | `CONSOLE_REGISTERVDM_MSG` | 40 |
| 15 | `0x0300000F` | ~~GetHardwareState~~ | `ServerDeprecatedApi` | `CONSOLE_GETHARDWARESTATE_MSG` | 8 |
| 16 | `0x03000010` | ~~SetHardwareState~~ | `ServerDeprecatedApi` | `CONSOLE_SETHARDWARESTATE_MSG` | 8 |
| 17 | `0x03000011` | `GetConsoleDisplayMode` | `ServerGetConsoleDisplayMode` | `CONSOLE_GETDISPLAYMODE_MSG` | 4 |
| 18 | `0x03000012` | `AddConsoleAlias` | `ServerAddConsoleAlias` | `CONSOLE_ADDALIAS_MSG` | 12 |
| 19 | `0x03000013` | `GetConsoleAlias` | `ServerGetConsoleAlias` | `CONSOLE_GETALIAS_MSG` | 12 |
| 20 | `0x03000014` | `GetConsoleAliasesLength` | `ServerGetConsoleAliasesLength` | `CONSOLE_GETALIASESLENGTH_MSG` | 8 |
| 21 | `0x03000015` | `GetConsoleAliasExesLength` | `ServerGetConsoleAliasExesLength` | `CONSOLE_GETALIASEXESLENGTH_MSG` | 8 |
| 22 | `0x03000016` | `GetConsoleAliases` | `ServerGetConsoleAliases` | `CONSOLE_GETALIASES_MSG` | 8 |
| 23 | `0x03000017` | `GetConsoleAliasExes` | `ServerGetConsoleAliasExes` | `CONSOLE_GETALIASEXES_MSG` | 8 |
| 24 | `0x03000018` | `ExpungeConsoleCommandHistory` | `ServerExpungeConsoleCommandHistory` | `CONSOLE_EXPUNGECOMMANDHISTORY_MSG` | 4 |
| 25 | `0x03000019` | `SetConsoleNumberOfCommands` | `ServerSetConsoleNumberOfCommands` | `CONSOLE_SETNUMBEROFCOMMANDS_MSG` | 8 |
| 26 | `0x0300001A` | `GetConsoleCommandHistoryLength` | `ServerGetConsoleCommandHistoryLength` | `CONSOLE_GETCOMMANDHISTORYLENGTH_MSG` | 8 |
| 27 | `0x0300001B` | `GetConsoleCommandHistory` | `ServerGetConsoleCommandHistory` | `CONSOLE_GETCOMMANDHISTORY_MSG` | 8 |
| 28 | `0x0300001C` | ~~SetKeyShortcuts~~ | `ServerDeprecatedApi` | `CONSOLE_SETKEYSHORTCUTS_MSG` | 5 |
| 29 | `0x0300001D` | ~~SetMenuClose~~ | `ServerDeprecatedApi` | `CONSOLE_SETMENUCLOSE_MSG` | 1 |
| 30 | `0x0300001E` | ~~GetKeyboardLayoutName~~ | `ServerDeprecatedApi` | `CONSOLE_GETKEYBOARDLAYOUTNAME_MSG` | 10 |
| 31 | `0x0300001F` | `GetConsoleWindow` | `ServerGetConsoleWindow` | `CONSOLE_GETCONSOLEWINDOW_MSG` | 8 |
| 32 | `0x03000020` | ~~CharType~~ | `ServerDeprecatedApi` | `CONSOLE_CHAR_TYPE_MSG` | 8 |
| 33 | `0x03000021` | ~~LocalEUDC~~ | `ServerDeprecatedApi` | `CONSOLE_LOCAL_EUDC_MSG` | 8 |
| 34 | `0x03000022` | ~~SetCursorMode~~ | `ServerDeprecatedApi` | `CONSOLE_CURSOR_MODE_MSG` | 2 |
| 35 | `0x03000023` | ~~GetCursorMode~~ | `ServerDeprecatedApi` | `CONSOLE_CURSOR_MODE_MSG` | 2 |
| 36 | `0x03000024` | ~~RegisterOS2~~ | `ServerDeprecatedApi` | `CONSOLE_REGISTEROS2_MSG` | 1 |
| 37 | `0x03000025` | ~~SetOS2OemFormat~~ | `ServerDeprecatedApi` | `CONSOLE_SETOS2OEMFORMAT_MSG` | 1 |
| 38 | `0x03000026` | ~~GetNlsMode~~ | `ServerDeprecatedApi` | `CONSOLE_NLS_MODE_MSG` | 8 |
| 39 | `0x03000027` | ~~SetNlsMode~~ | `ServerDeprecatedApi` | `CONSOLE_NLS_MODE_MSG` | 8 |
| 40 | `0x03000028` | `GetConsoleSelectionInfo` | `ServerGetConsoleSelectionInfo` | `CONSOLE_GETSELECTIONINFO_MSG` | 16 |
| 41 | `0x03000029` | `GetConsoleProcessList` | `ServerGetConsoleProcessList` | `CONSOLE_GETCONSOLEPROCESSLIST_MSG` | 4 |
| 42 | `0x0300002A` | `GetConsoleHistory` | `ServerGetConsoleHistory` | `CONSOLE_HISTORY_MSG` | 12 |
| 43 | `0x0300002B` | `SetConsoleHistory` | `ServerSetConsoleHistory` | `CONSOLE_HISTORY_MSG` | 12 |
| 44 | `0x0300002C` | `SetConsoleCurrentFont` | `ServerSetConsoleCurrentFont` | `CONSOLE_CURRENTFONT_MSG` | 84 |

统计:
- L1: 10 个 (含 2 个 deprecated) — 核心输入/输出/模式
- L2: 22 个 — Screen Buffer 全部操作
- L3: 45 个 (含 30 个 deprecated) — 字体/显示/别名/历史/选择，约 15 个活跃 API

---

## 第二部分：消息结构体完整定义

### 2.1 公共头 (`CONSOLE_MSG_HEADER`)

```c
typedef struct _CONSOLE_MSG_HEADER {
    ULONG ApiNumber;            // Layer<<24 | Index
    ULONG ApiDescriptorSize;    // 期望的返回结构体大小
} CONSOLE_MSG_HEADER;
```

### 2.2 CONSOLE_API_MSG — 核心消息包装

```c
typedef struct _CONSOLE_API_MSG {
    // ── 驱动完成元数据 ──
    CD_IO_COMPLETE Complete{};           // 偏移 0:  上一轮完成信息
    CONSOLE_API_STATE State{};           // 偏移 N:  I/O 状态 (WriteOffset/ReadOffset/BufferSize/Buffer指针)

    // ── 运行时辅助 ──
    IDeviceComm* _pDeviceComm{nullptr};  // 驱动通信接口
    IApiRoutines* _pApiRoutines{nullptr};// API 例程表
    til::small_vector<BYTE, 128> _inputBuffer;   // 输入数据缓冲
    til::small_vector<BYTE, 128> _outputBuffer;  // 输出数据缓冲

    // ── 数据包区域 (驱动覆盖写入) ──
    CD_IO_DESCRIPTOR Descriptor;         // 驱动消息描述符 (Identifier, Process, Object, Function, InputSize, OutputSize)
    union {
        // 对于 CONNECT 消息 (Function=0x01)
        struct {
            CD_CREATE_OBJECT_INFORMATION CreateObject;
            CONSOLE_CREATESCREENBUFFER_MSG CreateScreenBuffer;
        };
        // 对于 USER_DEFINED 消息 (Function=0x07)
        struct {
            CONSOLE_MSG_HEADER msgHeader;
            union {
                CONSOLE_MSG_BODY_L1 consoleMsgL1;
                CONSOLE_MSG_BODY_L2 consoleMsgL2;
                CONSOLE_MSG_BODY_L3 consoleMsgL3;
            } u;
        };
    };
} CONSOLE_API_MSG;
```

### 2.3 L1 消息结构体 (conmsgl1.h)

```c
// ── 公共: CONSOLE_SERVER_MSG (CONNECT 时携带的进程信息) ──
typedef struct _CONSOLE_SERVER_MSG {
    ULONG IconId, HotKey, StartupFlags;
    USHORT FillAttribute, ShowWindow;
    COORD ScreenBufferSize, WindowSize, WindowOrigin;
    ULONG ProcessGroupId;
    BOOLEAN ConsoleApp, WindowVisible;
    USHORT TitleLength;
    WCHAR  Title[MAX_PATH + 1];
    USHORT ApplicationNameLength;
    WCHAR  ApplicationName[128];
    USHORT CurrentDirectoryLength;
    WCHAR  CurrentDirectory[MAX_PATH + 1];
} CONSOLE_SERVER_MSG;

// ── L1 消息 ──
typedef struct _CONSOLE_GETCP_MSG {
    OUT ULONG CodePage;
    IN  BOOLEAN Output;             // TRUE=OutputCP, FALSE=InputCP
} CONSOLE_GETCP_MSG;                // sizeof = 8

typedef struct _CONSOLE_MODE_MSG {
    IN OUT ULONG Mode;
} CONSOLE_MODE_MSG;                 // sizeof = 4

typedef struct _CONSOLE_GETNUMBEROFINPUTEVENTS_MSG {
    OUT ULONG ReadyEvents;
} CONSOLE_GETNUMBEROFINPUTEVENTS_MSG; // sizeof = 4

typedef struct _CONSOLE_GETCONSOLEINPUT_MSG {
    OUT ULONG NumRecords;
    IN  USHORT Flags;          // 0=peek, 1=read
    IN  BOOLEAN Unicode;        // TRUE=UTF-16, FALSE=ANSI
} CONSOLE_GETCONSOLEINPUT_MSG;  // sizeof = 8

typedef struct _CONSOLE_READCONSOLE_MSG {
    IN  BOOLEAN Unicode;          // TRUE=返回UTF-16, FALSE=ANSI
    IN  BOOLEAN ProcessControlZ;  // TRUE=Ctrl+Z结束读取
    IN  USHORT ExeNameLength;     // 调用方exe名长度
    IN  ULONG  InitialNumBytes;   // 初始数据字节数 (从exe名后开始)
    IN  ULONG  CtrlWakeupMask;    // 哪些Ctrl键唤醒读取
    OUT ULONG  ControlKeyState;   // 最终Ctrl键状态
    OUT ULONG  NumBytes;           // 实际返回字节数
} CONSOLE_READCONSOLE_MSG;        // sizeof = 24

typedef struct _CONSOLE_WRITECONSOLE_MSG {
    OUT ULONG NumBytes;           // 实际写入字节数
    IN  BOOLEAN Unicode;           // TRUE=UTF-16, FALSE=ANSI
} CONSOLE_WRITECONSOLE_MSG;       // sizeof = 8

typedef struct _CONSOLE_LANGID_MSG {
    OUT LANGID LangId;
} CONSOLE_LANGID_MSG;             // sizeof = 2

typedef struct _CONSOLE_MAPBITMAP_MSG {    // deprecated
    OUT HANDLE Mutex;
    OUT PVOID Bitmap;
} CONSOLE_MAPBITMAP_MSG;          // sizeof = 16 (x64)
```

### 2.4 L2 消息结构体 (conmsgl2.h)

```c
// ── CreateScreenBuffer (特殊: 在 CONSOLE_API_MSG 的 CreateScreenBuffer 联合字段中) ──
typedef struct _CONSOLE_CREATESCREENBUFFER_MSG {
    IN ULONG Flags;            // CONSOLE_TEXTMODE_BUFFER (1) 总是
    IN ULONG BitmapInfoLength; // 总是 0
    IN ULONG Usage;            // 总是 0
} CONSOLE_CREATESCREENBUFFER_MSG;

// ── 数据编码类型常量 ──
#define CONSOLE_ASCII          0x1  // CHAR_INFO.Char.AsciiChar
#define CONSOLE_REAL_UNICODE   0x2  // CHAR_INFO.Char.UnicodeChar (CodePage==UTF-16)
#define CONSOLE_ATTRIBUTE      0x3  // 返回属性
#define CONSOLE_FALSE_UNICODE  0x4  // CodePage!=UTF-16, 转换为 MultiByte

typedef struct _CONSOLE_FILLCONSOLEOUTPUT_MSG {
    IN  COORD  WriteCoord;       // 起始坐标
    IN  ULONG  ElementType;      // CONSOLE_ASCII/ATTRIBUTE/UNICODE
    IN  USHORT Element;          // 字符或属性值
    IN OUT ULONG Length;          // 填充数量
} CONSOLE_FILLCONSOLEOUTPUT_MSG; // sizeof = 16

typedef struct _CONSOLE_CTRLEVENT_MSG {
    IN ULONG CtrlEvent;          // CTRL_C_EVENT / CTRL_BREAK_EVENT / etc
    IN ULONG ProcessGroupId;
} CONSOLE_CTRLEVENT_MSG;        // sizeof = 8

typedef struct _CONSOLE_SETCP_MSG {
    IN ULONG CodePage;
    IN BOOLEAN Output;           // TRUE=OutputCP, FALSE=InputCP
} CONSOLE_SETCP_MSG;            // sizeof = 8

typedef struct _CONSOLE_GETCURSORINFO_MSG {
    OUT ULONG   CursorSize;      // 1-100 (百分比)
    OUT BOOLEAN Visible;
} CONSOLE_GETCURSORINFO_MSG;    // sizeof = 8

typedef struct _CONSOLE_SETCURSORINFO_MSG {
    IN ULONG   CursorSize;
    IN BOOLEAN Visible;
} CONSOLE_SETCURSORINFO_MSG;    // sizeof = 8

typedef struct _CONSOLE_SCREENBUFFERINFO_MSG {
    IN OUT COORD    Size;                    // 缓冲区大小 (字符)
    IN OUT COORD    CursorPosition;
    IN OUT COORD    ScrollPosition;          // 视口左上角缓冲区坐标
    IN OUT USHORT   Attributes;              // 默认文本属性
    IN OUT COORD    CurrentWindowSize;       // 当前视口字符尺寸
    IN OUT COORD    MaximumWindowSize;       // 最大视口字符尺寸
    IN OUT USHORT   PopupAttributes;         // popup属性
    IN OUT BOOLEAN  FullscreenSupported;
    IN OUT COLORREF ColorTable[16];          // 16色调色板
} CONSOLE_SCREENBUFFERINFO_MSG; // sizeof = 88

typedef struct _CONSOLE_SETSCREENBUFFERSIZE_MSG {
    IN COORD Size;
} CONSOLE_SETSCREENBUFFERSIZE_MSG; // sizeof = 4

typedef struct _CONSOLE_SETCURSORPOSITION_MSG {
    IN COORD CursorPosition;
} CONSOLE_SETCURSORPOSITION_MSG;  // sizeof = 4

typedef struct _CONSOLE_GETLARGESTWINDOWSIZE_MSG {
    OUT COORD Size;
} CONSOLE_GETLARGESTWINDOWSIZE_MSG; // sizeof = 4

typedef struct _CONSOLE_SCROLLSCREENBUFFER_MSG {
    IN SMALL_RECT ScrollRectangle;    // 源矩形
    IN SMALL_RECT ClipRectangle;      // 目标剪裁矩形
    IN BOOLEAN    Clip;               // 是否剪裁
    IN BOOLEAN    Unicode;
    IN COORD      DestinationOrigin;  // 目标左上角
    IN CHAR_INFO  Fill;               // 填充字符
} CONSOLE_SCROLLSCREENBUFFER_MSG;     // sizeof = 32

typedef struct _CONSOLE_SETTEXTATTRIBUTE_MSG {
    IN USHORT Attributes;
} CONSOLE_SETTEXTATTRIBUTE_MSG;       // sizeof = 2

typedef struct _CONSOLE_SETWINDOWINFO_MSG {
    IN BOOLEAN    Absolute;            // TRUE=绝对坐标, FALSE=相对
    IN SMALL_RECT Window;              // 新窗口矩形
} CONSOLE_SETWINDOWINFO_MSG;          // sizeof = 12

typedef struct _CONSOLE_READCONSOLEOUTPUTSTRING_MSG {
    IN  COORD  ReadCoord;              // 起始坐标
    IN  ULONG  StringType;             // CONSOLE_ASCII/UNICODE
    OUT ULONG  NumRecords;             // 复制的字符数
} CONSOLE_READCONSOLEOUTPUTSTRING_MSG; // sizeof = 12

typedef struct _CONSOLE_WRITECONSOLEINPUT_MSG {
    OUT ULONG  NumRecords;             // 写入的 INPUT_RECORD 数
    IN  BOOLEAN Unicode;
    IN  BOOLEAN Append;                // TRUE=追加, FALSE=替换
} CONSOLE_WRITECONSOLEINPUT_MSG;        // sizeof = 12

typedef struct _CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG {
    IN  COORD  WriteCoord;
    IN  ULONG  StringType;
    OUT ULONG  NumRecords;
} CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG; // sizeof = 12

typedef struct _CONSOLE_WRITECONSOLEOUTPUT_MSG {
    IN OUT SMALL_RECT CharRegion;      // 写入的矩形区域
    IN     BOOLEAN    Unicode;
} CONSOLE_WRITECONSOLEOUTPUT_MSG;       // sizeof = 12

typedef struct _CONSOLE_READCONSOLEOUTPUT_MSG {
    IN OUT SMALL_RECT CharRegion;       // 读取的矩形区域
    IN     BOOLEAN    Unicode;
} CONSOLE_READCONSOLEOUTPUT_MSG;        // sizeof = 12

typedef struct _CONSOLE_GETTITLE_MSG {
    OUT ULONG  TitleLength;
    IN  BOOLEAN Unicode;
    IN  BOOLEAN Original;              // TRUE=原始标题, FALSE=当前
} CONSOLE_GETTITLE_MSG;               // sizeof = 8

typedef struct _CONSOLE_SETTITLE_MSG {
    IN BOOLEAN Unicode;
} CONSOLE_SETTITLE_MSG;               // sizeof = 4

// ── L2 联合体 ──
typedef union _CONSOLE_MSG_BODY_L2 {
    CONSOLE_CTRLEVENT_MSG              GenerateConsoleCtrlEvent;
    CONSOLE_FILLCONSOLEOUTPUT_MSG      FillConsoleOutput;
    CONSOLE_SETCP_MSG                  SetConsoleCP;
    CONSOLE_GETCURSORINFO_MSG          GetConsoleCursorInfo;
    CONSOLE_SETCURSORINFO_MSG          SetConsoleCursorInfo;
    CONSOLE_SCREENBUFFERINFO_MSG       GetConsoleScreenBufferInfo;
    CONSOLE_SCREENBUFFERINFO_MSG       SetConsoleScreenBufferInfo;
    CONSOLE_SETSCREENBUFFERSIZE_MSG    SetConsoleScreenBufferSize;
    CONSOLE_SETCURSORPOSITION_MSG      SetConsoleCursorPosition;
    CONSOLE_GETLARGESTWINDOWSIZE_MSG   GetLargestConsoleWindowSize;
    CONSOLE_SCROLLSCREENBUFFER_MSG     ScrollConsoleScreenBuffer;
    CONSOLE_SETTEXTATTRIBUTE_MSG       SetConsoleTextAttribute;
    CONSOLE_SETWINDOWINFO_MSG          SetConsoleWindowInfo;
    CONSOLE_READCONSOLEOUTPUTSTRING_MSG ReadConsoleOutputString;
    CONSOLE_WRITECONSOLEINPUT_MSG      WriteConsoleInput;
    CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG WriteConsoleOutputString;
    CONSOLE_WRITECONSOLEOUTPUT_MSG     WriteConsoleOutput;
    CONSOLE_READCONSOLEOUTPUT_MSG      ReadConsoleOutput;
    CONSOLE_SETTITLE_MSG               SetConsoleTitle;
    CONSOLE_GETTITLE_MSG               GetConsoleTitle;
} CONSOLE_MSG_BODY_L2;
```

### 2.5 L3 消息结构体 (conmsgl3.h — 仅活跃部分)

```c
typedef struct _CONSOLE_GETSELECTIONINFO_MSG {
    OUT CONSOLE_SELECTION_INFO SelectionInfo;
} CONSOLE_GETSELECTIONINFO_MSG;

typedef struct _CONSOLE_GETMOUSEINFO_MSG {
    OUT ULONG NumButtons;
} CONSOLE_GETMOUSEINFO_MSG;

// font 相关
typedef struct _CONSOLE_GETFONTSIZE_MSG {
    IN  ULONG  FontIndex;
    OUT COORD  FontSize;
} CONSOLE_GETFONTSIZE_MSG;

typedef struct _CONSOLE_CURRENTFONT_MSG {
    IN  BOOLEAN MaximumWindow;
    IN OUT ULONG FontIndex;
    IN OUT COORD FontSize;
    IN OUT ULONG FontFamily;
    IN OUT ULONG FontWeight;
    IN OUT WCHAR FaceName[LF_FACESIZE]; // 32 WCHAR
} CONSOLE_CURRENTFONT_MSG;

// display mode
typedef struct _CONSOLE_SETDISPLAYMODE_MSG {
    IN  ULONG dwFlags;           // CONSOLE_FULLSCREEN_MODE / CONSOLE_WINDOWED_MODE
    OUT COORD ScreenBufferDimensions;
} CONSOLE_SETDISPLAYMODE_MSG;

typedef struct _CONSOLE_GETDISPLAYMODE_MSG {
    OUT ULONG ModeFlags;
} CONSOLE_GETDISPLAYMODE_MSG;

// 别名 (Alias)
typedef struct _CONSOLE_ADDALIAS_MSG {
    IN USHORT SourceLength;
    IN USHORT TargetLength;
    IN USHORT ExeLength;
    IN BOOLEAN Unicode;
} CONSOLE_ADDALIAS_MSG;

typedef struct _CONSOLE_GETALIAS_MSG {
    IN  USHORT SourceLength;
    OUT USHORT TargetLength;
    IN  USHORT ExeLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETALIAS_MSG;

typedef struct _CONSOLE_GETALIASESLENGTH_MSG {
    OUT ULONG  AliasesLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETALIASESLENGTH_MSG;

typedef struct _CONSOLE_GETALIASEXESLENGTH_MSG {
    OUT ULONG  AliasExesLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETALIASEXESLENGTH_MSG;

typedef struct _CONSOLE_GETALIASES_MSG {
    IN  BOOLEAN Unicode;
    OUT ULONG  AliasesBufferLength;
} CONSOLE_GETALIASES_MSG;

typedef struct _CONSOLE_GETALIASEXES_MSG {
    OUT ULONG  AliasExesBufferLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETALIASEXES_MSG;

// 命令历史 (CommandHistory)
typedef struct _CONSOLE_EXPUNGECOMMANDHISTORY_MSG {
    IN BOOLEAN Unicode;
} CONSOLE_EXPUNGECOMMANDHISTORY_MSG;

typedef struct _CONSOLE_SETNUMBEROFCOMMANDS_MSG {
    IN ULONG  NumCommands;
    IN BOOLEAN Unicode;
} CONSOLE_SETNUMBEROFCOMMANDS_MSG;

typedef struct _CONSOLE_GETCOMMANDHISTORYLENGTH_MSG {
    OUT ULONG  CommandHistoryLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETCOMMANDHISTORYLENGTH_MSG;

typedef struct _CONSOLE_GETCOMMANDHISTORY_MSG {
    OUT ULONG  CommandBufferLength;
    IN  BOOLEAN Unicode;
} CONSOLE_GETCOMMANDHISTORY_MSG;

// 窗口句柄
typedef struct _CONSOLE_GETCONSOLEWINDOW_MSG {
    OUT HWND hwnd;
} CONSOLE_GETCONSOLEWINDOW_MSG;

// 进程列表
typedef struct _CONSOLE_GETPROCESSLIST_MSG {
    OUT ULONG dwProcessCount;
} CONSOLE_GETCONSOLEPROCESSLIST_MSG;

// 历史信息
typedef struct _CONSOLE_GETHISTORY_MSG {
    OUT ULONG HistoryBufferSize;
    OUT ULONG NumberOfHistoryBuffers;
    OUT ULONG dwFlags;
} CONSOLE_HISTORY_MSG; // 同时用于 GetConsoleHistory 和 SetConsoleHistory
```

---

## 第三部分：核心数据结构

### 3.1 CONSOLE_INFORMATION (全局主机状态)

```cpp
class CONSOLE_INFORMATION : public Settings, public IIoProvider {
public:
    ConsoleProcessList ProcessHandleList;     // 所有附加进程链表
    InputBuffer* pInputBuffer = nullptr;      // 全局输入缓冲区
    SCREEN_INFORMATION* ScreenBuffers = nullptr; // screen buffer 单链表头
    ConsoleWaitQueue OutputQueue;             // 输出等待队列

    DWORD Flags = 0;                          // CONSOLE_IS_ICONIC / CONSOLE_HAS_FOCUS / ...
    UINT CP = 0;                              // 当前输入代码页
    UINT OutputCP = 0;                        // 当前输出代码页
    UINT DefaultCP = 0;                       // RIS 重置用默认输入CP
    UINT DefaultOutputCP = 0;                 // RIS 重置用默认输出CP
    ULONG CtrlFlags = 0;                      // 待处理的 Ctrl 事件
    ULONG LimitingProcessId = 0;

    CPINFO CPInfo = {};
    CPINFO OutputCPInfo = {};

    // 线程安全
    til::recursive_ticket_lock _lock;         // 控制台全局锁 (递归)

    // VT 支持
    VtIo _vtIo;
    MidiAudio _midiAudio;

    // 渲染数据
    RenderData renderData;

private:
    std::wstring _Title;
    std::wstring _Prefix;
    std::wstring _TitleAndPrefix;
    std::wstring _OriginalTitle;
    std::wstring _LinkTitle;
    SCREEN_INFORMATION* pCurrentScreenBuffer = nullptr;
    COOKED_READ_DATA* _cookedReadData = nullptr;
    bool _bracketedPasteMode = false;
    std::optional<std::wstring> _pendingClipboardText;
};
```

**全局位标志 (Flags)**:
```
CONSOLE_IS_ICONIC               0x00000001  // 窗口最小化
CONSOLE_OUTPUT_SUSPENDED        0x00000002  // 输出暂停
CONSOLE_HAS_FOCUS               0x00000004  // 拥有键盘焦点
CONSOLE_IGNORE_NEXT_MOUSE_INPUT 0x00000008  // 跳过下次鼠标输入
CONSOLE_SELECTING               0x00000010  // 正在选择
CONSOLE_SCROLLING               0x00000020  // 正在滚动
CONSOLE_UPDATING_SCROLL_BARS    0x00000400
CONSOLE_QUICK_EDIT_MODE         0x00000800
CONSOLE_CONNECTED_TO_EMULATOR   0x00002000
CONSOLE_QUIT_POSTED             0x00008000  // 退出已投递
CONSOLE_AUTO_POSITION           0x00010000
CONSOLE_IGNORE_NEXT_KEYUP       0x00020000
CONSOLE_HISTORY_NODUP           0x00100000
CONSOLE_SCROLLBAR_TRACKING      0x00200000
CONSOLE_SETTING_WINDOW_SIZE     0x00800000
CONSOLE_USE_PRIVATE_FLAGS       0x20000000
CONSOLE_INITIALIZED             0x80000000  // 控制台已初始化
```

### 3.2 SCREEN_INFORMATION (Screen Buffer)

```cpp
class SCREEN_INFORMATION : public ConsoleObjectHeader, public IIoProvider {
public:
    DWORD OutputMode;  // ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT (默认)
    short WheelDelta = 0;
    short HWheelDelta = 0;
    UINT ScrollScale = 1;
    SCREEN_INFORMATION* Next = nullptr;           // 链表下一节点
    BYTE WriteConsoleDbcsLeadByte[2] = {0, 0};   // DBCS 前导字节
    BYTE FillOutDbcsLeadChar = 0;                // FillConsoleOutput DBCS 前导

    // ── 私有状态 ──
private:
    IWindowMetrics* _pConsoleWindowMetrics;       // 窗口度量接口
    std::unique_ptr<TextBuffer> _textBuffer;      // ★ 文本缓冲区
    ConhostInternalGetSet _api{*this};             // ★ VT 适配器 API 实现
    std::shared_ptr<StateMachine> _stateMachine;   // ★ VT 输出状态机 (解析VT序列)
    Viewport _viewport;                            // 视口 (可见区域)
    SCREEN_INFORMATION* _psiAlternateBuffer = nullptr;  // 备选 buffer (VT xterm alt screen)
    SCREEN_INFORMATION* _psiMainBuffer = nullptr;       // 主 buffer 回指针
    til::rect _rcAltSavedClientNew;
    til::rect _rcAltSavedClientOld;
    bool _fAltWindowChanged = false;
    TextAttribute _PopupAttributes;               // 弹出窗口属性
    FontInfo _currentFont;                        // 当前字体
    FontInfoDesired _desiredFont;                 // 期望字体
    til::CoordType _virtualBottom = 0;            // 虚拟底部 (API 级视口跟踪)
    std::optional<til::size> _deferredPtyResize;  // 延迟 PTY resize
    std::atomic<bool> _conptyCursorPositionMayBeWrong = false;
};
```

**OutputMode 位标志 (与 Win32 `SetConsoleMode` 对应)**:
```
ENABLE_PROCESSED_OUTPUT  0x0001  // 处理 \r\n → \r\n, \t expand
ENABLE_WRAP_AT_EOL_OUTPUT 0x0002 // 行尾自动换行
```

### 3.3 TextBuffer (文本缓冲区)

```cpp
class TextBuffer final {
public:
    TextBuffer(til::size screenBufferSize, TextAttribute defaultAttributes,
               UINT cursorSize, bool isActiveBuffer, Renderer* renderer);

    // ── Row 访问 ──
    ROW& GetScratchpadRow();                         // 暂存行 (用于临时操作)
    ROW& GetScratchpadRow(const TextAttribute& attributes);
    const ROW& GetRowByOffset(til::CoordType index) const;
    ROW& GetMutableRowByOffset(til::CoordType index);

    // ── 迭代器 ──
    TextBufferCellIterator GetCellDataAt(til::point at) const;
    TextBufferCellIterator GetCellLineDataAt(til::point at) const;
    TextBufferTextIterator GetTextDataAt(til::point at) const;
    TextBufferTextIterator GetTextLineDataAt(til::point at) const;

    // ── 光标 ──
    Cursor& GetCursor() noexcept;
    const Cursor& GetCursor() const noexcept;

    // ── 写入 ──
    OutputCellIterator Write(const OutputCellIterator givenIt);
    OutputCellIterator Write(const OutputCellIterator givenIt,
                             til::point target, std::optional<bool> wrap = true);
    OutputCellIterator WriteLine(const OutputCellIterator givenIt,
                                 til::point target,
                                 std::optional<bool> setWrap = std::nullopt,
                                 std::optional<til::CoordType> limitRight = std::nullopt);

    void Replace(til::CoordType row, const TextAttribute& attributes, RowWriteState& state);
    void Insert(til::CoordType row, const TextAttribute& attributes, RowWriteState& state);
    void FillRect(const til::rect& rect, const std::wstring_view& fill, const TextAttribute& attributes);

    // ── 滚动 ──
    void ScrollRows(til::CoordType firstRow, til::CoordType size, til::CoordType delta);
    void IncrementCircularBuffer(const TextAttribute& fillAttributes = {});

    // ── 尺寸 ──
    til::CoordType TotalRowCount() const noexcept;
    const Viewport GetSize() const noexcept;
    void ResizeTraditional(til::size newSize);
    void Reset() noexcept;

    // ── 属性 ──
    const TextAttribute& GetCurrentAttributes() const noexcept;
    void SetCurrentAttributes(const TextAttribute& currentAttributes) noexcept;
    void SetWrapForced(til::CoordType y, bool wrap);
    void SetCurrentLineRendition(LineRendition lineRendition, const TextAttribute& fillAttributes);
    LineRendition GetLineRendition(til::CoordType row) const;
    bool IsDoubleWidthLine(til::CoordType row) const;

    // ── 最后非空格 ──
    til::point GetLastNonSpaceCharacter(const Viewport* viewOptional = nullptr) const;

    // ── 通知/重绘 ──
    void TriggerRedraw(const Viewport& viewport);
    void TriggerRedrawAll();
    void TriggerScroll();
    void TriggerScroll(til::point delta);
    void TriggerNewTextNotification(const std::wstring_view newText);
    void TriggerSelection();

    // ── 文字操作 ──
    std::wstring GetPlainText(til::point start, til::point end) const;
    std::wstring GetPlainText(const CopyRequest& req) const;
    static void Reflow(TextBuffer& oldBuffer, TextBuffer& newBuffer, ...);

    // ── 查找 ──
    std::optional<std::vector<til::point_span>> SearchText(const std::wstring_view& needle, SearchFlag flags) const;

    // ── 标记 (Shell Integration) ──
    std::vector<ScrollMark> GetMarkRows() const;
    void StartPrompt();
    bool StartCommand();
    bool StartOutput();
    void EndCurrentCommand(std::optional<unsigned int> error);
    void SetScrollbarData(ScrollbarData mark, til::CoordType y);

    // ── 超链接 ──
    void AddHyperlinkToMap(std::wstring_view uri, uint16_t id);
    std::wstring GetHyperlinkUriFromId(uint16_t id) const;
    uint16_t GetHyperlinkId(std::wstring_view uri, std::wstring_view id);
    void RemoveHyperlinkFromMap(uint16_t id) noexcept;

    uint64_t GetLastMutationId() const noexcept;
    const til::CoordType GetFirstRowIndex() const noexcept;

private:
    // ── 虚拟内存管理 ──
    // TextBuffer 使用 VirtualAlloc(MEM_RESERVE) 预分配整个缓冲区,
    // 然后按需 MEM_COMMIT。ROW 在内存中的布局:
    //   ROW                <-- sizeof(ROW)
    //   (padding for alignment)
    //   ROW::_charsBuffer  <-- _width * sizeof(wchar_t)
    //   (padding)
    //   ROW::_charOffsets  <-- (_width + 1) * sizeof(uint16_t)
    //   (padding)
    //   ...                <-- 下一个 ROW
    //
    // 由于逐行调用 VirtualAlloc(MEM_COMMIT) 极慢, 采用批量提交策略:
    // _commitWatermark 记录已提交的最高行 + 128 行预读,
    // 每次 Commitment 以 128 行为一批。
    //   80 列最小:  60KB 块, 9001 行 = 4.1MB
    //  120 列常见:  80KB 块, 9001 行 = 5.6MB
    //  400 列最大: 220KB 块, 9001 行 = 15.5MB
    wil::unique_virtualalloc_ptr<std::byte> _buffer;  // 内存区域基地址
    std::byte* _bufferEnd = nullptr;                  // 内存区域末尾
    std::byte* _commitWatermark = nullptr;             // 已提交范围上限 (批量提交分界线)
    static constexpr size_t _commitReadAheadRowCount = 128; // 预读行数
    TextAttribute _initialAttributes;                  // 初始化属性 (用于新提交的 ROW)
    size_t _bufferRowStride = 0;                       // 单行步长 (ROW + padding + chars + offsets)
    size_t _bufferOffsetChars = 0;                     // ROW::_charsBuffer 在 stride 内的偏移
    size_t _bufferOffsetCharOffsets = 0;               // ROW::_charOffsets 在 stride 内的偏移
    uint16_t _width = 0;                               // 缓冲区宽度 (列数)
    uint16_t _height = 0;                              // 缓冲区高度 (行数, 不含 scratchpad)

    // ── 内部方法 ──
    void _reserve(til::size screenBufferSize, const TextAttribute& defaultAttributes);
    void _commit(const std::byte* row);                // 提交单行内存 (MEM_COMMIT)
    void _decommit() noexcept;
    void _construct(const std::byte* until) noexcept;  // 构造 ROW 对象
    void _destroy() const noexcept;
    ROW& _getRowByOffsetDirect(size_t offset);
    ROW& _getRow(til::CoordType y) const;
    til::CoordType _estimateOffsetOfLastCommittedRow() const noexcept;
    bool _isRowCommitted(til::CoordType y) const noexcept;
    void _SetFirstRowIndex(const til::CoordType FirstRowIndex) noexcept;

    // ── 其他内部状态 ──
    Renderer* _renderer = nullptr;
    TextAttribute _currentAttributes;
    til::CoordType _firstRow = 0;                      // 环形缓冲区顶部索引 (未必是 ROW[0])
    uint64_t _lastMutationId = 0;                      // 最后修改 ID (用于 UI 增量更新)
    Cursor _cursor;
    bool _isActiveBuffer = false;

    // ── 超链接映射 ──
    std::unordered_map<uint16_t, std::wstring> _hyperlinkMap;       // ID → URI
    std::unordered_map<std::wstring, uint16_t> _hyperlinkCustomIdMap; // customID → ID
    uint16_t _currentHyperlinkId = 1;

    // ── 文本处理辅助 ──
    void _ExpandTextRow(til::inclusive_rect& selectionRow) const;
    DelimiterClass _GetDelimiterClassAt(const til::point pos, const std::wstring_view wordDelimiters) const;
    til::point _GetDelimiterClassRunStart(...) const;
    til::point _GetDelimiterClassRunEnd(...) const;
    void _PruneHyperlinks();
    std::wstring _commandForRow(...) const;
    MarkExtents _scrollMarkExtentForRow(...) const;
    bool _createPromptMarkIfNeeded();
    std::tuple<til::CoordType, til::CoordType, bool> _RowCopyHelper(...) const;
    void _SerializeRow(...) const;
    static void _AppendRTFText(std::string& contentBuilder, const std::wstring_view& text);
};
```

### 3.4 ROW (行存储)

```cpp
class ROW {
    // ── 字符存储 ──
    wchar_t _charsBuffer[内部固定大小];  // 默认内联存储
    std::unique_ptr<wchar_t[]> _charsHeap; // 溢出堆存储 (长行/宽字符)
    std::span<wchar_t> _chars;             // 实际字符数据视图

    // ── 列偏移映射 ──
    // _charOffsets[col] = 该列对应在 _chars 中的字符偏移
    //   bit 31 = 宽字符的后半部分标记
    //   _charOffsets[_columnCount] = past-the-end 偏移
    std::span<uint16_t> _charOffsets;

    // ── 属性 ──
    // 游程编码 (RLE): "5 BLUE, 3 RED, ..."
    // 全行同属性时内联存储, 否则堆分配
    RowAttributes _attr;              // RLE 属性对

    // ── 元数据 ──
    uint16_t _columnCount;            // 行宽 (字符列)
    LineRendition _lineRendition;     // SingleWidth / DoubleWidthTop / DoubleWidthBottom
    bool _wrapForced;                 // 强制换行 (非自然换行)
    bool _doubleBytePadded;          // DBCS 填充

    // ── 可选扩展 ──
    std::optional<PromptData> _promptData;   // Shell 集成用
    std::optional<ImageSlice> _imageSlice;   // Sixel/图像用
};
```

**LineRendition**:
```
SingleWidth       = 0  // 正常单宽行
DoubleWidthTop    = 1  // DECDWL 上半个双宽行
DoubleWidthBottom = 2  // DECDWL 下半个双宽行
```

### 3.5 InputBuffer (输入缓冲区)

```cpp
class InputBuffer final : public ConsoleObjectHeader {
public:
    DWORD InputMode;                          // ENABLE_LINE_INPUT / ENABLE_ECHO_INPUT / ENABLE_PROCESSED_INPUT / ...
    ConsoleWaitQueue WaitQueue;               // ReadConsole 等待队列

    // ── 字符串 API ──
    void Consume(bool isUnicode, std::wstring_view& source, std::span<char>& target);
    void ConsumeCached(bool isUnicode, std::span<char>& target);
    void Cache(std::wstring_view source);

    // ── INPUT_RECORD API ──
    size_t ConsumeCached(bool isUnicode, size_t count, InputEventQueue& target);
    size_t PeekCached(bool isUnicode, size_t count, InputEventQueue& target);
    void Cache(bool isUnicode, InputEventQueue& source, size_t expectedSourceSize);

    // ── 存储 ──
    void ReinitializeInputBuffer();
    void WakeUpReadersWaitingForData();
    size_t GetNumberOfReadyEvents() const noexcept;
    void Flush();
    void FlushAllButKeys();

    // ── Read (阻塞等待) ──
    NTSTATUS Read(InputEventQueue& OutEvents, size_t AmountToRead,
                  bool Peek, bool WaitForData, bool Unicode, bool Stream);

    // ── 写入 ──
    size_t Prepend(const std::span<const INPUT_RECORD>& inEvents);
    size_t Write(const INPUT_RECORD& inEvent);
    size_t Write(const std::span<const INPUT_RECORD>& inEvents);
    void WriteString(const std::wstring_view& text);
    void WriteFocusEvent(bool focused) noexcept;
    bool WriteMouseEvent(til::point position, unsigned int button, short keyState, short wheelDelta);

    // ── VT 输入 ──
    bool IsInVirtualTerminalInputMode() const;
    TerminalInput& GetTerminalInput();

    bool IsWritePartialByteSequenceAvailable() const noexcept;
    const INPUT_RECORD& FetchWritePartialByteSequence() noexcept;

private:
    // 缓存: 字符串模式或 INPUT_RECORD 模式之间切换
    std::string _cachedTextA;
    std::wstring _cachedTextW;
    std::deque<INPUT_RECORD> _cachedInputEvents;
    ReadingMode _readingMode;

    // 主存储
    std::deque<INPUT_RECORD> _storage;
    INPUT_RECORD _writePartialByteSequence{};
    bool _writePartialByteSequenceAvailable = false;

    TerminalInput _termInput;  // VT 输入状态机
};
```

**InputMode 位标志**:
```
ENABLE_PROCESSED_INPUT  0x0001  // CTRL+C → 信号, CR→CR, Backspace处理
ENABLE_LINE_INPUT       0x0002  // 行模式 (ReadConsole 返回整行)
ENABLE_ECHO_INPUT       0x0004  // 回显
ENABLE_WINDOW_INPUT     0x0008  // 窗口尺寸改变 → INPUT_RECORD
ENABLE_MOUSE_INPUT      0x0010  // 鼠标事件
ENABLE_INSERT_MODE      0x0020  // 插入模式
ENABLE_QUICK_EDIT_MODE  0x0040  // 快速编辑
ENABLE_EXTENDED_FLAGS   0x0080  // 启用扩展标志
ENABLE_AUTO_POSITION    0x0100  // 自动定位
ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200  // VT 输入处理
```

### 3.6 COOKED_READ_DATA (行编辑状态)

```cpp
class COOKED_READ_DATA final : public ReadData {
    // 构造函数:
    //   COOKED_READ_DATA(InputBuffer* pInputBuffer,
    //       INPUT_READ_HANDLE_DATA* pInputReadHandleData,
    //       SCREEN_INFORMATION& screenInfo,
    //       size_t UserBufferSize, char* UserBuffer,
    //       ULONG CtrlWakeupMask,
    //       std::wstring_view exeName,
    //       std::wstring_view initialData,
    //       ConsoleProcessHandle* pClientProcess);

    bool Read(bool isUnicode, size_t& numBytes, ULONG& controlKeyState);
    bool Notify(WaitTerminationReason TerminationReason,
                bool fIsUnicode,
                NTSTATUS* pReplyStatus,
                size_t* pNumBytes,
                DWORD* pControlKeyState,
                void* pOutputData) noexcept override;

    void EraseBeforeResize();           // resize 前清理
    void RedrawAfterResize();           // resize 后重绘
    void SetInsertMode(bool insertMode) noexcept;
    bool IsEmpty() const noexcept;
    bool PresentingPopup() const noexcept; // 是否展示弹出窗口 (tab completion)
    til::point_span GetBoundaries() noexcept;

private:
    enum class State : uint8_t {
        // 行编辑子状态
        Accumulate,        // 累积字符
        Echo,              // 回显
        WaitForCompletion, // 等待输入完成
    };
    // 内部维护:
    // - 用户缓冲区指针
    // - 当前编辑行内容 (std::wstring)
    // - 光标位置 (列)
    // - 插入/覆盖模式标志
    // - popup (tab completion) 状态
};
```

### 3.7 Cursor (光标)

```cpp
class Cursor {
    til::point _position;        // 当前位置 (0-based)
    ULONG _size;                 // 1-100 (百分比)
    bool _visible;               // 是否可见
    bool _blinkingAllowed;       // 允许闪烁
    bool _isDouble;              // 双倍高度光标 (DBCS)
    bool _isOn;                  // 闪烁周期状态
    CursorType _type;            // Legacy / VerticalBar / Underscore / ...
    COLORREF _color;             // 光标颜色
    TextAttribute _attributes;   // 光标位置处的当前文本属性
    bool _delayEOLWrap;          // 延迟行尾换行 (最后一个字符写入)
};
```

### 3.8 FontInfo / FontInfoDesired

```cpp
class FontInfo {
    til::size _size;                    // 字符尺寸 (像素)
    UINT _family;                       // FF_DONTCARE | FF_MODERN | ...
    UINT _weight;                       // FW_NORMAL | FW_BOLD | ...
    std::wstring _faceName;             // "Consolas" / "Cascadia Code" / ...
    UINT _codePage;                     // 字体代码页
    bool _trueTypeFont;                 // TrueType vs 位图字体
};

class FontInfoDesired {
    til::size _size;
    UINT _family;
    UINT _weight;
    std::wstring _faceName;
};
```

### 3.9 TextAttribute (文本属性 / SGR)

每个字符格子 (`CHAR_INFO`) 包含:
```cpp
struct TextAttribute {
    // ── 前景/背景色 ──
    // 支持: 16 色 (传统) / 256 色 (xterm) / 24-bit 真彩色
    // 内部存储: 4-bit legacy index + extended color table pointer
    // IsLegacy(): 使用传统 4-bit 颜色
    // IsDefault(): 使用终端默认颜色
    // IsRgb(): RGB 真彩色

    // ── 元属性 ──
    bool _bold;
    bool _italic;
    bool _underline;
    bool _doublyUnderline;
    bool _curlyUnderline;
    bool _dottedUnderline;
    bool _dashedUnderline;
    bool _overline;
    bool _blinking;
    bool _invisible;
    bool _crossedOut;
    bool _faint;
    bool _reverseVideo;
    bool _protected;             // DECSCA 保护
    bool _hyperlink;             // 超链接
};
```

### 3.10 CHAR_INFO

```cpp
typedef struct _CHAR_INFO {
    union {
        WCHAR UnicodeChar;
        CHAR  AsciiChar;
    } Char;
    WORD Attributes; // TextAttribute 的 16-bit 传统编码
} CHAR_INFO;
```

### 3.11 INPUT_RECORD

```cpp
typedef struct _INPUT_RECORD {
    WORD  EventType;  // KEY_EVENT(1) / MOUSE_EVENT(2) / WINDOW_BUFFER_SIZE_EVENT(4) / MENU_EVENT(8) / FOCUS_EVENT(16)
    union {
        KEY_EVENT_RECORD          KeyEvent;
        MOUSE_EVENT_RECORD        MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
        MENU_EVENT_RECORD         MenuEvent;
        FOCUS_EVENT_RECORD        FocusEvent;
    } Event;
} INPUT_RECORD;

typedef struct _KEY_EVENT_RECORD {
    BOOL  bKeyDown;
    WORD  wRepeatCount;
    WORD  wVirtualKeyCode;
    WORD  wVirtualScanCode;
    union {
        WCHAR UnicodeChar;
        CHAR  AsciiChar;
    } uChar;
    DWORD dwControlKeyState; // RIGHT_ALT_PRESSED / LEFT_CTRL_PRESSED / SHIFT_PRESSED / ...
} KEY_EVENT_RECORD;

typedef struct _MOUSE_EVENT_RECORD {
    COORD dwMousePosition;
    DWORD dwButtonState;
    DWORD dwControlKeyState;
    DWORD dwEventFlags; // MOUSE_MOVED / DOUBLE_CLICK / MOUSE_WHEELED / MOUSE_HWHEELED
} MOUSE_EVENT_RECORD;
```

---

## 第四部分：虚拟终端 (VT) 管线

### 4.1 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                        CONSOLE_INFORMATION                        │
│                                                                  │
│  ┌──────────────────────┐         ┌──────────────────────────┐  │
│  │     VtIo              │         │   SCREEN_INFORMATION       │  │
│  │                      │         │                          │  │
│  │  ┌────────────────┐  │         │  ┌────────────────────┐  │  │
│  │  │ VtInputThread  │  │         │  │   StateMachine     │  │  │
│  │  │ (VT→Console)    │──┼──写入──→│  │   (解析VT输出序列)  │  │  │
│  │  │                │  │         │  │                    │  │  │
│  │  │ ReadFile(pipe) │  │         │  │ ProcessString(wstr)│  │  │
│  │  │ UTF-8→UTF-16   │  │         │  │  → IStateMachine   │  │  │
│  │  │ ProcessString() │  │         │  │    Engine回调      │  │  │
│  │  └───────┬────────┘  │         │  └─────────┬──────────┘  │  │
│  │          │           │         │            │              │  │
│  │          ▼           │         │            ▼              │  │
│  │  ┌────────────────┐  │         │  ┌────────────────────┐  │  │
│  │  │InputState      │  │         │  │OutputStateMachine  │  │  │
│  │  │MachineEngine   │  │         │  │Engine              │  │  │
│  │  │                │  │         │  │                    │  │  │
│  │  │解析VT输入序列   │  │         │  │ C0/C1/ESC/CSI/OSC  │  │  │
│  │  │→ InteractDispatch│  │        │  │ → ITermDispatch   │  │  │
│  │  └───────┬────────┘  │         │  └─────────┬──────────┘  │  │
│  │          │           │         │            │              │  │
│  │          ▼           │         │            ▼              │  │
│  │  ┌────────────────┐  │         │  ┌────────────────────┐  │  │
│  │  │InteractDispatch│  │         │  │  AdaptDispatch     │  │  │
│  │  │                │  │         │  │  (ITermDispatch)    │  │  │
│  │  │ VT事件→        │  │         │  │                    │  │  │
│  │  │ INPUT_RECORD   │  │         │  │  VT命令→           │  │  │
│  │  │ → InputBuffer  │  │         │  │  TextBuffer操作    │  │  │
│  │  └────────────────┘  │         │  │  → ConhostInternal  │  │  │
│  │                      │         │  │    GetSet API       │  │  │
│  │  ┌────────────────┐  │         │  └─────────┬──────────┘  │  │
│  │  │PtySignalInput  │  │         │            │              │  │
│  │  │Thread          │  │         │            ▼              │  │
│  │  │                │  │         │  ┌────────────────────┐  │  │
│  │  │信号管道: resize│  │         │  │ConhostInternalGetSet│  │  │
│  │  │/clear/showhide │  │         │  │                    │  │  │
│  │  └────────────────┘  │         │  │ SCREEN_INFORMATION  │  │  │
│  │                      │         │  │ / TextBuffer        │  │  │
│  │  ┌────────────────┐  │         │  │ / Cursor 操作       │  │  │
│  │  │ Writer          │  │         │  └────────────────────┘  │  │
│  │  │                │  │         │                          │  │
│  │  │ 写VT到终端      │◀─┼─Write ─│ (通过 WriteCharsVT)      │  │
│  │  └────────────────┘  │         │                          │  │
│  └──────────────────────┘         └──────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    InputBuffer                             │   │
│  │  std::deque<INPUT_RECORD> _storage                        │   │
│  │  TerminalInput _termInput (VT输入模式下的状态)             │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 StateMachine (VT 解析器)

```cpp
class StateMachine {
    // 状态:
    //   Ground → Escape → CsiEntry → CsiParam → CsiIntermediate → CsiIgnore
    //          → DcsEntry → DcsParam → DcsIntermediate → DcsPassThrough → DcsIgnore
    //          → OscString → OscParam
    //          → Ss3Entry → Ss3Param

    // 核心方法:
    void ProcessString(const std::wstring_view string);  // 输入VT序列
    void ProcessCharacter(const wchar_t wch);            // 单字符输入

    // 引擎绑定:
    void SetEngine(IStateMachineEngine* engine);
    IStateMachineEngine* Engine();

    // 状态查询:
    bool IsInGroundState() const;
    // ...
};
```

**VT 解析状态转换图**:
```
Ground --ESC--> Escape --CSI_dispatch-->
                 |                      v
                 └─ 0x9B (CSI 8-bit) ──→ CsiEntry --digit/semicolon--> CsiParam
                 |                                             |
                 └─ 0x90 (DCS 8-bit) ──→ DcsEntry              | 0x20-0x2F
                 |                                             v
                 └─ 0x9D (OSC 8-bit) ──→ OscString      CsiIntermediate
                 |                                             | 0x30-0x3F (private marker)
                 └─ 0x8E (SS2 8-bit) ──→ Ss2Entry              v
                 |                                       CSI dispatched → Ground
                 └─ 0x8F (SS3 8-bit) ──→ Ss3Entry
```

### 4.3 IStateMachineEngine 接口

```cpp
class IStateMachineEngine {
public:
    virtual ~IStateMachineEngine() = 0;
    virtual void UnknownSequence() noexcept = 0;
    virtual bool EncounteredWin32InputModeSequence() const noexcept = 0;

    virtual bool ActionExecute(const wchar_t wch) = 0;               // C0 控制字符 (BEL, BS, TAB, CR, LF...)
    virtual bool ActionExecuteFromEscape(const wchar_t wch) = 0;     // ESC 转义后单字符
    virtual bool ActionPrint(const wchar_t wch) = 0;                 // 可打印字符
    virtual bool ActionPrintString(const std::wstring_view string) = 0; // 可打印字符串

    virtual bool ActionPassThroughString(const std::wstring_view string) = 0;

    virtual bool ActionEscDispatch(const VTID id) = 0;                        // ESC 序列
    virtual bool ActionVt52EscDispatch(const VTID id, const VTParameters parameters) = 0; // VT52 ESC
    virtual bool ActionCsiDispatch(const VTID id, const VTParameters parameters) = 0;     // CSI 序列
    virtual StringHandler ActionDcsDispatch(const VTID id, const VTParameters parameters) = 0; // DCS 序列 (返回数据接收器)
    virtual bool ActionOscDispatch(const size_t parameter, const std::wstring_view string) = 0; // OSC 序列
    virtual bool ActionSs3Dispatch(const wchar_t wch, const VTParameters parameters) = 0;      // SS3 序列

protected:
    IStateMachineEngine() = default;
};
```

### 4.4 OutputStateMachineEngine (输出 VT 解析)

实现 `IStateMachineEngine`，将 VT 序列分派到 `ITermDispatch` 操作:

```
ActionExecute:
  BEL → WarningBell()
  BS  → CursorBackward(1)
  TAB → ForwardTab(1)
  CR  → CarriageReturn()
  LF  → LineFeed()
  ...

ActionEscDispatch:
  '7' → CursorSaveState()
  '8' → CursorRestoreState()
  'D' → CursorNextLine(1)   (Index)
  'M' → CursorPrevLine(1)   (Reverse Index)
  'E' → CursorNextLine(1)   (NEL)
  ...

ActionCsiDispatch:
  'A' → CursorUp(params)
  'B' → CursorDown(params)
  'C' → CursorForward(params)
  'D' → CursorBackward(params)
  'H' → CursorPosition(line, col)
  'J' → EraseInDisplay(type)
  'K' → EraseInLine(type)
  'm' → SetGraphicsRendition(params)     ★ SGR (颜色/粗体/斜体/下划线...)
  'h' → SetMode(params)                  ★ DECSET
  'l' → ResetMode(params)                ★ DECRST
  'n' → DeviceStatusReport(type)
  'r' → SetTopBottomScrollingMargins(top, bottom)
  's' → CursorSaveState()
  'u' → CursorRestoreState()
  '@' → InsertCharacter(count)
  'L' → InsertLine(count)
  'M' → DeleteLine(count)
  'P' → DeleteCharacter(count)
  'S' → ScrollUp(count)
  'T' → ScrollDown(count)
  'X' → EraseCharacters(count)
  ...
```

### 4.5 ITermDispatch 接口 (全部虚方法)

这是 VT 适配器层最重要的接口，定义了所有终端操作:

```cpp
class ITermDispatch {
public:
    using StringHandler = std::function<bool(const wchar_t)>;

    enum class OptionalFeature { ChecksumReport, ClipboardWrite };

    // ── 基本 I/O ──
    virtual void Print(const wchar_t wchPrintable) = 0;
    virtual void PrintString(const std::wstring_view string) = 0;

    // ── 光标移动 ──
    virtual void CursorUp(const VTInt distance) = 0;         // CUU
    virtual void CursorDown(const VTInt distance) = 0;       // CUD
    virtual void CursorForward(const VTInt distance) = 0;    // CUF
    virtual void CursorBackward(const VTInt distance) = 0;   // CUB
    virtual void CursorNextLine(const VTInt distance) = 0;   // CNL
    virtual void CursorPrevLine(const VTInt distance) = 0;   // CPL
    virtual void CursorHorizontalPositionAbsolute(const VTInt column) = 0; // CHA
    virtual void VerticalLinePositionAbsolute(const VTInt line) = 0;       // VPA
    virtual void HorizontalPositionRelative(const VTInt distance) = 0;     // HPR
    virtual void VerticalPositionRelative(const VTInt distance) = 0;       // VPR
    virtual void CursorPosition(const VTInt line, const VTInt column) = 0; // CUP
    virtual void CursorSaveState() = 0;                       // DECSC
    virtual void CursorRestoreState() = 0;                    // DECRC

    // ── 编辑 ──
    virtual void InsertCharacter(const VTInt count) = 0;      // ICH
    virtual void DeleteCharacter(const VTInt count) = 0;      // DCH
    virtual void InsertLine(const VTInt distance) = 0;        // IL
    virtual void DeleteLine(const VTInt distance) = 0;        // DL
    virtual void InsertColumn(const VTInt distance) = 0;      // DECIC
    virtual void DeleteColumn(const VTInt distance) = 0;      // DECDC

    // ── 滚动 ──
    virtual void ScrollUp(const VTInt distance) = 0;          // SU
    virtual void ScrollDown(const VTInt distance) = 0;        // SD
    virtual void NextPage(const VTInt pageCount) = 0;         // NP
    virtual void PrecedingPage(const VTInt pageCount) = 0;    // PP
    virtual void PagePositionAbsolute(const VTInt page) = 0;  // PPA
    virtual void PagePositionRelative(const VTInt pageCount) = 0; // PPR
    virtual void PagePositionBack(const VTInt pageCount) = 0; // PPB
    virtual void RequestDisplayedExtent() = 0;                // DECRQDE

    // ── 模式设置 ──
    virtual void SetKeypadMode(const bool applicationMode) = 0;  // DECANM
    virtual void SetAnsiMode(const bool ansiMode) = 0;           // DECANM
    virtual void SetTopBottomScrollingMargins(const VTInt top, const VTInt bottom) = 0; // DECSTBM
    virtual void SetLeftRightScrollingMargins(const VTInt left, const VTInt right) = 0; // DECSLRM

    // ── 换行/回车 ──
    virtual void CarriageReturn() = 0;
    virtual void LineFeed(const DispatchTypes::LineFeedType lineFeedType) = 0;
    virtual void ReverseLineFeed() = 0;        // RI
    virtual void BackIndex() = 0;
    virtual void ForwardIndex() = 0;

    // ── 窗口标题 ──
    virtual void SetWindowTitle(std::wstring_view title) = 0;
    virtual void SetCurrentWorkingDirectory(const std::wstring_view uri) = 0;

    // ── 制表符 ──
    virtual void HorizontalTabSet() = 0;        // HTS
    virtual void ForwardTab(const VTInt numTabs) = 0;  // CHT
    virtual void BackwardsTab(const VTInt numTabs) = 0; // CBT
    virtual void TabClear(const DispatchTypes::TabClearType clearType) = 0; // TBC
    virtual void TabSet(const VTParameter setType) = 0;

    // ── 颜色 ──
    virtual void SetColorTableEntry(const size_t tableIndex, const DWORD color) = 0;
    virtual void RequestColorTableEntry(const size_t tableIndex) = 0;
    virtual void ResetColorTable() = 0;
    virtual void ResetColorTableEntry(const size_t tableIndex) = 0;
    virtual void AssignColor(const DispatchTypes::ColorItem item, const VTInt fgIndex, const VTInt bgIndex) = 0;

    // ── 擦除 ──
    virtual void EraseInDisplay(const DispatchTypes::EraseType eraseType) = 0; // ED
    virtual void EraseInLine(const DispatchTypes::EraseType eraseType) = 0;    // EL
    virtual void EraseCharacters(const VTInt numChars) = 0;                    // ECH
    virtual void SelectiveEraseInDisplay(const DispatchTypes::EraseType eraseType) = 0; // DECSED
    virtual void SelectiveEraseInLine(const DispatchTypes::EraseType eraseType) = 0;    // DECSEL

    // ── 矩形操作 ──
    virtual void ChangeAttributesRectangularArea(const VTInt top, ...) = 0; // DECCARA
    virtual void ReverseAttributesRectangularArea(const VTInt top, ...) = 0; // DECRARA
    virtual void CopyRectangularArea(...) = 0;     // DECCRA
    virtual void FillRectangularArea(const VTParameter ch, ...) = 0; // DECFRA
    virtual void EraseRectangularArea(...) = 0;    // DECERA
    virtual void SelectiveEraseRectangularArea(...) = 0; // DECSERA

    // ── SGR (图形渲染) ──
    virtual void SetGraphicsRendition(const VTParameters options) = 0;  // SGR ★
    virtual void SetLineRendition(const LineRendition rendition) = 0;   // DECDWL/DECDHL
    virtual void SetCharacterProtectionAttribute(const VTParameters options) = 0; // DECSCA
    virtual void PushGraphicsRendition(const VTParameters options) = 0; // XTPUSHCOLORS
    virtual void PopGraphicsRendition() = 0;                             // XTPOPCOLORS

    // ── 模式设置 ──
    virtual void SetMode(const DispatchTypes::ModeParams param) = 0;  // DECSET
    virtual void ResetMode(const DispatchTypes::ModeParams param) = 0; // DECRST
    virtual void RequestMode(const DispatchTypes::ModeParams param) = 0; // DECRQM

    // ── 设备报告 ──
    virtual void DeviceStatusReport(const DispatchTypes::StatusType statusType, ...) = 0; // DSR
    virtual void DeviceAttributes() = 0;          // DA1
    virtual void SecondaryDeviceAttributes() = 0; // DA2
    virtual void TertiaryDeviceAttributes() = 0;  // DA3
    virtual void Vt52DeviceAttributes() = 0;
    virtual void RequestTerminalParameters(...) = 0; // DECREQTPARM

    // ── 字符集 ──
    virtual void DesignateCodingSystem(const VTID codingSystem) = 0;
    virtual void Designate94Charset(const VTInt gsetNumber, const VTID charset) = 0;
    virtual void Designate96Charset(const VTInt gsetNumber, const VTID charset) = 0;
    virtual void LockingShift(const VTInt gsetNumber) = 0;
    virtual void LockingShiftRight(const VTInt gsetNumber) = 0;
    virtual void SingleShift(const VTInt gsetNumber) = 0;

    // ── 复位 ──
    virtual void SoftReset() = 0;               // DECSTR
    virtual void HardReset(bool erase) = 0;     // RIS
    virtual void ScreenAlignmentPattern() = 0;  // DECALN

    // ── 光标样式 ──
    virtual void SetCursorStyle(const DispatchTypes::CursorStyle cursorStyle) = 0; // DECSCUSR

    // ── 剪贴板 ──
    virtual void SetClipboard(wil::zwstring_view content) = 0;  // OSC 52

    // ── 窗口操作 ──
    virtual void WindowManipulation(const DispatchTypes::WindowManipulationType function,
                                    const VTParameter parameter1,
                                    const VTParameter parameter2) = 0;  // DECSWP

    // ── 超链接 ──
    virtual void AddHyperlink(const std::wstring_view uri, const std::wstring_view params) = 0; // OSC 8
    virtual void EndHyperlink() = 0;

    // ── 扩展序列 (ConEmu / iTerm2 / FinalTerm / VSCode / WT) ──
    virtual void DoConEmuAction(const std::wstring_view string) = 0;   // OSC 9;4
    virtual void DoITerm2Action(const std::wstring_view string) = 0;   // OSC 1337
    virtual void DoFinalTermAction(const std::wstring_view string) = 0; // OSC 133
    virtual void DoVsCodeAction(const std::wstring_view string) = 0;   // OSC 633
    virtual void DoWTAction(const std::wstring_view string) = 0;       // OSC 9001

    // ── Sixel / DRCS ──
    virtual StringHandler DefineSixelImage(...) = 0;
    virtual StringHandler DownloadDRCS(...) = 0;

    // ── 宏 ──
    virtual StringHandler DefineMacro(...) = 0;  // DECDMAC
    virtual void InvokeMacro(const VTInt macroId) = 0;

    // ── 状态报告/恢复 ──
    virtual void RequestTerminalStateReport(...) = 0;
    virtual StringHandler RestoreTerminalState(...) = 0;
    virtual StringHandler RequestSetting() = 0;
    virtual void RequestPresentationStateReport(...) = 0;
    virtual StringHandler RestorePresentationState(...) = 0;

    // ── 声音 ──
    virtual void PlaySounds(const VTParameters parameters) = 0;

    // ── 可选特性 ──
    virtual void SetOptionalFeatures(const til::enumset<OptionalFeature> features) = 0;
};
```

### 4.6 IInteractDispatch 接口 (VT 输入 → Console)

```cpp
class IInteractDispatch {
public:
    virtual ~IInteractDispatch() = default;
    virtual bool IsVtInputEnabled() const = 0;

    virtual void WriteInput(const std::span<const INPUT_RECORD>& inputEvents) = 0;  // 写入 INPUT_RECORD
    virtual void WriteCtrlKey(const INPUT_RECORD& event) = 0;                        // Ctrl+C/Break
    virtual void WriteString(std::wstring_view string) = 0;                           // 写入字符串 (处理模式)
    virtual void WriteStringRaw(std::wstring_view string) = 0;                        // 写入字符串 (原始模式)
    virtual void WindowManipulation(DispatchTypes::WindowManipulationType function,
                                    VTParameter parameter1, VTParameter parameter2) = 0;
    virtual void MoveCursor(VTInt row, VTInt col) = 0;                               // 光标重定位
    virtual void FocusChanged(bool focused) = 0;                                      // 焦点事件
};
```

### 4.7 VtIo (VT I/O 协调器)

```cpp
class VtIo {
public:
    // ── Writer (内部类) ──
    struct Writer {
        void Submit();                          // 刷新缓冲到管道
        void BackupCursor() const;
        void WriteUTF8(std::string_view str) const;
        void WriteUTF16(std::wstring_view str) const;
        void WriteUTF16TranslateCRLF(std::wstring_view str) const;  // \n→\r\n
        void WriteUTF16StripControlChars(std::wstring_view str) const;
        void WriteUCS2(wchar_t ch) const;
        void WriteCUP(til::point position) const;    // 光标定位
        void WriteDECTCEM(bool enabled) const;       // 光标显隐
        void WriteSGR1006(bool enabled) const;       // SGR 1006
        void WriteDECAWM(bool enabled) const;        // 自动换行
        void WriteASB(bool enabled) const;           // 备用 screen buffer
        bool WriteDSRCPR() const;                    // 请求光标位置
        void WriteWindowVisibility(bool visible) const;
        void WriteWindowTitle(std::wstring_view title) const;
        void WriteAttributes(const TextAttribute& attributes) const;   // 属性→SGR序列
        void WriteInfos(til::point target, std::span<const CHAR_INFO> infos) const;
        void WriteScreenInfo(SCREEN_INFORMATION& newContext, til::size oldSize) const;
    private:
        VtIo* _io = nullptr;
    };

    // ── VtIo 方法 ──
    HRESULT Initialize(const ConsoleArguments* const pArgs);
    bool IsUsingVt() const;
    HRESULT StartIfNeeded();
    void Shutdown() noexcept;

    void SetDeviceAttributes(til::enumset<DeviceAttribute, uint64_t> attributes) noexcept;
    til::enumset<DeviceAttribute, uint64_t> GetDeviceAttributes() const noexcept;

    void SendCloseEvent();
    void CreatePseudoWindow();

private:
    enum class State : uint8_t {
        Uninitialized,   // 未初始化
        Initialized,     // 已初始化 (有管道句柄)
        Starting,        // 正在启动 (等待 DA1)
        StartupFailed,   // 启动失败
        Running,         // 正常运行
    };

    wil::unique_hfile _hInput;          // PTY 输入管道 (ReadFile → VT输入)
    wil::unique_hfile _hOutput;         // PTY 输出管道 (WriteFile → VT输出)
    wil::unique_hfile _hSignal;         // 信号管道

    std::unique_ptr<VtInputThread> _pVtInputThread;           // VT 输入线程
    std::unique_ptr<PtySignalInputThread> _pPtySignalInputThread; // 信号线程

    til::enumset<DeviceAttribute, uint64_t> _deviceAttributes;

    // 双缓冲: _front = 当前正在发送, _back = 正在写入
    std::string _front;
    std::string _back;
    OVERLAPPED* _overlapped = nullptr;
    OVERLAPPED _overlappedBuf{};
    wil::unique_event _overlappedEvent;
    bool _overlappedPending = false;
    bool _writerRestoreCursor = false;
    bool _writerTainted = false;

    State _state = State::Uninitialized;
    bool _lookingForCursorPosition = false;
    bool _closeEventSent = false;
    int _corked = 0;     // corking 计数 (批量输出)
};
```

### 4.8 VtInputThread (VT → Console 输入)

```cpp
class VtInputThread {
    // 线程循环: ReadFile(pipe) → UTF-8→UTF-16 →
    //            StateMachine::ProcessString(wstr) → InputStateMachineEngine
    //
    // 关键成员:
    wil::unique_hfile _hFile;         // 输入管道句柄
    VtIo* _pVtIo;                     // 父 VtIo 指针

    static DWORD WINAPI StaticVtInputThreadProc(LPVOID lpParameter);
    void _Run();                       // 主循环
};
```

**VtInputThread 数据流**:
```
PTY 管道 (VT输入)
    │
    ▼ ReadFile
    │
UTF-8 字节流
    │ MultiByteToWideChar(CP_UTF8)
    ▼
UTF-16 宽字符流
    │ StateMachine::ProcessString()
    ▼
InputStateMachineEngine
    │ 解析 VT 输入序列:
    │   CSI A B C D → 方向键 → KEY_EVENT_RECORD
    │   CSI M → 鼠标事件
    │   ESC [ I / O → Focus 事件
    │   Win32Input 模式 → 完整 INPUT_RECORD
    ▼
InteractDispatch::WriteInput()
    │
    ▼
InputBuffer::Write(inputEvents)
    │
    ▼
WaitQueue → 唤醒 ReadConsole
```

### 4.9 PtySignalInputThread (信号管道)

```cpp
class PtySignalInputThread {
    // 线程循环: ReadFile(signalPipe) → 解析消息 → 执行操作
    //
    // 支持的消息 (PtySignal.h):
    //   ShowHideWindow(show)
    //   ClearBuffer()
    //   ResizeWindow(width, height)
    //   SetParent(handle)
    //   CreatePseudoWindow(owner)
    //   ClosePseudoWindow()
    //
    // 管道断开 → SendCloseEvent() → 退出信号
};
```

**信号管道消息协议**:
```
┌─────┬───────────┐
│ 1B  │ Payload   │
├─────┼───────────┤
│ 0x01│ ShowHide  │
│ 0x02│ Clear     │
│ 0x03│ Resize    │
│ 0x04│ SetParent │
│ 0x05│ CreatePw  │
│ 0x06│ ClosePw   │
└─────┴───────────┘
```

### 4.10 ConhostInternalGetSet (VT ↔ Screen Buffer 桥)

```cpp
class ConhostInternalGetSet final : public ITerminalApi {
public:
    ConhostInternalGetSet(IIoProvider& io);

    // ── ITerminalApi 方法 ──
    void UnknownSequence() noexcept override;
    void ReturnResponse(const std::wstring_view response) override;  // 向终端返回响应 (DA/DSR)

    bool IsConPTY() const noexcept override;
    StateMachine& GetStateMachine() override;
    BufferState GetBufferAndViewport() override;
    void SetViewportPosition(const til::point position) override;

    void SetSystemMode(const Mode mode, const bool enabled) override;
    bool GetSystemMode(const Mode mode) const override;

    void ReturnAnswerback() override;
    void WarningBell() override;

    void SetWindowTitle(const std::wstring_view title) override;

    void UseAlternateScreenBuffer(const TextAttribute& attrs) override;
    void UseMainScreenBuffer() override;

    CursorType GetUserDefaultCursorStyle() const override;

    void ShowWindow(bool showOrHide) override;
    bool ResizeWindow(const til::CoordType width, const til::CoordType height) override;

    void SetCodePage(const unsigned int codepage) override;
    void ResetCodePage() override;
    unsigned int GetOutputCodePage() const override;
    unsigned int GetInputCodePage() const override;

    void CopyToClipboard(const wil::zwstring_view content) override;
    void SetTaskbarProgress(const TaskbarState state, const size_t progress) override;
    void SetWorkingDirectory(const std::wstring_view uri) override;
    void PlayMidiNote(const int noteNumber, const int velocity,
                      const std::chrono::microseconds duration) override;

    bool IsVtInputEnabled() const override;
    void NotifyBufferRotation(const int delta) override;
    void NotifyShellIntegrationMark() override;

private:
    IIoProvider& _io;  // 指向 SCREEN_INFORMATION / CONSOLE_INFORMATION
};
```

---

## 第五部分：完整数据流向

### 5.1 写入路径 (WriteConsole / 输出)

```
═══════════════════════════════════════════════════════════════
客户端进程 (cmd.exe / pwsh.exe)
    │
    │ WriteConsoleW() / WriteFile(CONOUT$)
    ▼
Kernel32 / ConDrv (内核驱动)
    │ IOCTL: CONSOLE_IO_RAW_WRITE / CONSOLE_IO_USER_DEFINED
    ▼
ConDrv → Server I/O 线程
    │ IoSorter::ServiceIoOperation
    │   → RAW_WRITE (Function=0x05)
    │   → USER_DEFINED (Function=0x07) → ApiSorter → L1 index 6 → ServerWriteConsole
    ▼
ServerWriteConsole (ApiDispatchers.cpp)
    │ 1. 提取 CONSOLE_WRITECONSOLE_MSG: Unicode? NumBytes?
    │ 2. 读取客户端数据: Message->ReadMessageInput(offset, data, size)
    │ 3. 调用 DoSrvWriteConsole(screenInfo, data, size, isUnicode)
    ▼
DoSrvWriteConsole (host/_stream.cpp)
    │ 分支判断:
    ├─ VT I/O 模式 (IsInVtIoMode):
    │   ├─ ANSI: MultiByteToWideChar(OutputCP) → wstr
    │   └─ WriteCharsVT(screenInfo, wstr)
    │       └─ screenInfo.GetStateMachine().ProcessString(wstr)
    │            → OutputStateMachineEngine → ITermDispatch (AdaptDispatch)
    │                → 更新 TextBuffer (光标位置、字符、属性)
    │                → 同时将相同 VT 序列镜像写入 VtIo::Writer → PTY 输出管道
    │
    └─ 传统模式 (非 VT):
        └─ WriteCharsLegacy(screenInfo, data, size, isUnicode)
            → 直接修改 TextBuffer + 通知渲染器重绘

═══════════════════════════════════════════════════════════════
```

**WriteConsole VT 模式的详细数据流**:

```
ServerWriteConsole
  │
  ▼
DoSrvWriteConsole
  │
  ▼
WriteCharsVT(screenInfo, wstr)
  │
  │ screenInfo.GetStateMachine().ProcessString(wstr)
  │   │
  │   ▼
  │ OutputStateMachineEngine::ActionPrint('H')
  │   → AdaptDispatch::Print('H')
  │     → TextBuffer::GetCursor()::Position()
  │     → TextBuffer::Write(OutputCellIterator{'H', currentAttr})
  │       → ROW::WriteCells(...)  // 写入 _chars + _charOffsets + _attr
  │     → Cursor::IncrementPosition()
  │
  │ OutputStateMachineEngine::ActionPrint('e')
  │   → (同上)
  │
  │ OutputStateMachineEngine::ActionExecute('\r')
  │   → AdaptDispatch::CarriageReturn()
  │     → Cursor::SetXPosition(0)
  │
  │ OutputStateMachineEngine::ActionExecute('\n')
  │   → AdaptDispatch::LineFeed(...)
  │     → 滚动或光标下移
  │       → TextBuffer::IncrementCircularBuffer() (如有必要)
  │       → Cursor::IncrementYPosition()
  │
  ▼
  [同时] VtIo::Writer::WriteUTF16TranslateCRLF(wstr)
    → 将相同的 VT 序列镜像写入 _back buffer
    → Submit() → WriteFile(hOutput, data) → PTY → 终端
```

### 5.2 读取路径 (ReadConsole / 输入)

```
═══════════════════════════════════════════════════════════════
数据来源有两类:

A. 传统输入 (键盘/鼠标) → InputBuffer
    Win32 消息循环 → HandleKeyEvent / HandleMouseEvent
      → 转换为 INPUT_RECORD
      → InputBuffer::Write(event)
        → _storage.push_back(event)
        → _wakeupReadersImpl()

B. VT 输入 (PTY 管道) → VtInputThread → InputBuffer
    PTY 输入管道 ReadFile → UTF-8→UTF-16
      → StateMachine::ProcessString(wstr)
        → InputStateMachineEngine
          → InteractDispatch::WriteInput(events)
            → InputBuffer::Write(events)
              → _storage.push_back(events)
              → _wakeupReadersImpl()

═══════════════════════════════════════════════════════════════
客户端 ReadConsole 流程:

客户端进程 (cmd.exe)
    │
    │ ReadConsoleW() / ReadFile(CONIN$)
    ▼
Kernel32 → ConDrv
    │ IOCTL: CONSOLE_IO_RAW_READ / CONSOLE_IO_USER_DEFINED
    ▼
ServerReadConsole (ApiDispatchers.cpp)
    │ 1. 提取 CONSOLE_READCONSOLE_MSG: Unicode?, CtrlWakeupMask, ExeName...
    │ 2. 判断输入模式:
    │
    ├─ 行模式 (ENABLE_LINE_INPUT):
    │   └─ DoSrvReadConsoleCooked(...)
    │       → 创建 COOKED_READ_DATA
    │       → 进入行编辑循环:
    │           while (!lineComplete) {
    │             InputBuffer::Read(events, ...)  // 阻塞等待
    │             for each KEY_EVENT_RECORD:
    │               if Enter → lineComplete = true
    │               if Backspace → 删除前一字符, 回显退格
    │               if printable → 追加到行, 回显
    │               if Tab → 尝试补全 (popup)
    │               if Up/Down → 历史
    │           }
    │       → 返回完整行
    │
    └─ 原始模式 (!ENABLE_LINE_INPUT):
        └─ DoSrvReadConsoleRaw(...)
            → InputBuffer::Read(events, ...)  // 阻塞等待
            → 返回所有可用事件或指定数量

    返回数据:
    │ Message->SetReplyStatus(STATUS_SUCCESS)
    │ Message->SetReplyInformation(numBytes)
    │ 数据写入 _outputBuffer → CD_IO_COMPLETE 完成
    ▼
ConDrv → 客户端进程
    │ ReadConsoleW 返回
    ▼
客户端进程接收输入数据
```

### 5.3 VT 输出完整管线 (CSI / SGR 序列)

```
═══════════════════════════════════════════════════════════════
示例: WriteConsole("Hello\x1b[31mRed\x1b[0m")

1. ServerWriteConsole 接到消息
2. DoSrvWriteConsole 编码为 wstr
3. WriteCharsVT(screenInfo, wstr)
   │
4. StateMachine::ProcessString("Hello\x1b[31mRed\x1b[0m")
   │
5. Ground 状态: ActionPrint('H') → AdaptDispatch::Print('H')
   │ → TextBuffer 写入 'H' + 当前属性
   │
6. ActionPrint('e'), 'l', 'l', 'o' → (同上)
   │
7. 0x1B (ESC): Ground→Escape
8. '[' (0x5B): Escape→CsiEntry
9. '3', '1': CsiParam (参数 31)
10. 'm' (0x6D): CsiDispatch('m', {31})
    │ OutputStateMachineEngine::ActionCsiDispatch('m', {31})
    │ → AdaptDispatch::SetGraphicsRendition({31})
    │   → TextAttribute 设置前景色为红色 (index 1)
    │   → SetCurrentAttributes(红色属性)
    │   → [同时] VtIo::Writer::WriteAttributes(红色属性)
    │       → 生成 SGR 序列 "\x1b[31m" 写入 PTY
    │
11. ActionPrint('R') → AdaptDispatch::Print('R') (用红色属性)
12. ActionPrint('e'), 'd' → (同上)
13. ESC [ 0 m: CsiDispatch('m', {0})
    │ → AdaptDispatch::SetGraphicsRendition({0})
    │   → TextAttribute 重置为默认
    │   → [同时] Writer::WriteAttributes(默认属性) → "\x1b[0m"
```

### 5.4 DCS / OSC / 扩展序列流向

```
OSC (Operating System Command):
    ESC ] 参数 ; 数据 ST (或 BEL)
    
    例: OSC 0 ; "My Title" BEL
    → StateMachine: Escape→OscString
    → AccumulateUntil(BEL|ST)
    → ActionOscDispatch(0, "My Title")
      → AdaptDispatch::SetWindowTitle("My Title")
        → ConhostInternalGetSet::SetWindowTitle("My Title")
          → CONSOLE_INFORMATION::SetTitle("My Title")
          → [镜像] Writer::WriteWindowTitle("My Title")

DCS (Device Control String):
    例: DCS $ q ... ST  (Sixel 图像)
    → StateMachine: Escape→DcsEntry→DcsPassThrough
    → ActionDcsDispatch('q', params)
      → 返回 StringHandler (lambda)
    → 数据字节逐个传递给 StringHandler
    → AdaptDispatch::DefineSixelImage(...)
```

### 5.5 控制台子系统的初始化流程

```
═══════════════════════════════════════════════════════════════
系统启动控制台进程时的初始化链:

1. CSRSS 创建进程 → 分配 ConDrv Server 句柄
   │
2. conhost.exe 启动 (或 corehost.exe)
   │
3. wWinMain → ConsoleCreateIoThread
   │
4. ConsoleServerInit:
   ├─ 创建 InputAvailableEvent
   ├─ SetServerInformation → IOCTL_SET_SERVER
   ├─ ConsoleAllocateConsole(title)
   │   ├─ 创建 CONSOLE_INFORMATION
   │   │   ├─ 创建 InputBuffer (pInputBuffer)
   │   │   ├─ 创建初始 SCREEN_INFORMATION (ScreenBuffers 链表)
   │   │   │   ├─ 创建 TextBuffer (size, defaultAttr, cursor)
   │   │   │   ├─ 创建 Cursor
   │   │   │   ├─ _InitializeOutputStateMachine()
   │   │   │   │   └─ new StateMachine → OutputStateMachineEngine
   │   │   │   │       → AdaptDispatch(ConhostInternalGetSet)
   │   │   │   └─ 设置 pCurrentScreenBuffer = 此 buffer
   │   │   └─ 初始化代码页 (GetConsoleCP/GetConsoleOutputCP)
   │   └─ SetActiveScreenBuffer(*screenInfo)
   │
5. VtIo::Initialize(args)
   ├─ 保存 _hInput / _hOutput / _hSignal 管道句柄
   ├─ State → Initialized
   │
6. VtIo::StartIfNeeded()
   ├─ 创建 VtInputThread (如果 _hInput 有效)
   ├─ 发送初始 VT 握手序列:
   │   ├─ "\x1b[c"            — DA1: 请求设备属性
   │   ├─ "\x1b[?1004h"       — Focus Event Mode ON
   │   └─ "\x1b[?9001h"       — Win32 Input Mode ON
   ├─ Writer::Submit() — 批量刷新
   ├─ VtInputThread::WaitUntilDA1(3000) — 等待终端 DA1 响应
   ├─ PtySignalInputThread::ConnectConsole()
   └─ State → Running
   │
7. 进入 I/O 消息循环 (ConsoleIoThread)
   for (;;) {
     ReadIo(ReplyMsg, &ReceiveMsg)
     if (PENDING) continue;
     ServiceIoOperation(ReceiveMsg)
       // → 分派到 IoSorter → ApiSorter → ApiDispatchers
   }
```

### 5.6 CONNECT 流程 (客户端连接)

```
═══════════════════════════════════════════════════════════════
客户端进程调用 AllocConsole() / AttachConsole() 时:

1. 客户端进程 → ConDrv → IOCTL: CONSOLE_IO_CONNECT (Function=0x01)
   │
2. 服务端收到 CONNECT 消息
   │
3. IoSorter::ServiceIoOperation → CONSOLE_IO_CONNECT
   │
4. ConsoleHandleConnectionRequest (IoDispatchers.cpp)
   │
5. 检查是否移交 (Delegation):
   ├─ 读取注册表 DelegationConsole CLSID
   ├─ 若配置了 CLSID:
   │   ├─ CoCreateInstance(CLSID, ..., IConsoleHandoff)
   │   └─ EstablishHandoff(server, event, &msg, signal, inbox, &proc)
   │       → 第三方终端接管
   │
   └─ 否则 (本地处理):
        ├─ accept_connection: 创建 \Input / \Output 句柄
        ├─ 创建 ConsoleProcessHandle → 加入 ProcessHandleList
        └─ complete_io (CONNECT 完成)
```

---

## 第六部分：关键枚举与常量汇总

### 6.1 ConsoleFunc (ConDrv 消息类型)

```
CONSOLE_IO_CONNECT        = 0x01  // 客户端连接
CONSOLE_IO_DISCONNECT     = 0x02  // 客户端断开
CONSOLE_IO_CREATE_OBJECT  = 0x03  // 创建对象句柄
CONSOLE_IO_CLOSE_OBJECT   = 0x04  // 关闭对象句柄
CONSOLE_IO_RAW_WRITE      = 0x05  // WriteConsole 等效
CONSOLE_IO_RAW_READ       = 0x06  // ReadConsole 等效
CONSOLE_IO_USER_DEFINED   = 0x07  // L1/L2/L3 Console API
CONSOLE_IO_RAW_FLUSH      = 0x08  // Flush
```

### 6.2 CD_IO_OBJECT_TYPE

```
CD_IO_OBJECT_TYPE_CURRENT_INPUT   = 0x01
CD_IO_OBJECT_TYPE_CURRENT_OUTPUT  = 0x02
CD_IO_OBJECT_TYPE_NEW_OUTPUT      = 0x03
CD_IO_OBJECT_TYPE_GENERIC         = 0x04
```

### 6.3 VtIo::Writer 生成的 VT 序列

| Writer 方法 | VT 序列 | 说明 |
|------------|---------|------|
| `WriteCUP({y,x})` | `CSI y ; x H` | 光标定位 |
| `WriteDECTCEM(true)` | `CSI ? 25 h` | 显示光标 |
| `WriteDECTCEM(false)` | `CSI ? 25 l` | 隐藏光标 |
| `WriteSGR1006(true)` | `CSI ? 1006 h` | SGR 鼠标协议 |
| `WriteDECAWM(true)` | `CSI ? 7 h` | 启用自动换行 |
| `WriteASB(true)` | `CSI ? 1049 h` | 启用备用 screen buffer |
| `WriteAttributes(attr)` | `CSI Ps m` | SGR 序列 (颜色/属性) |
| `WriteWindowTitle(title)` | `OSC 0 ; title BEL` | 设置窗口标题 |

---

## 第七部分：corehost 当前实现对照

| 原始组件 | 数据/功能 | corehost 实现状态 |
|---------|----------|-----------------|
| **CONSOLE_INFORMATION** | 全局主机状态, ScreenBuffer链表, InputBuffer, VtIo | **无** — 轻量级, 无 screen buffer |
| **SCREEN_INFORMATION** | ScreenBuffer封装, TextBuffer, StateMachine, Viewport, Font | **无** — 无渲染 |
| **TextBuffer** | 文本存储, ROW数组, Cursor, 属性, 超链接 | **无** — 纯字节转发 |
| **ROW** | 字符列, 字符偏移, RLE属性 | **无** |
| **InputBuffer** | INPUT_RECORD 队列, WaitQueue | **无** — 不做输入事件处理 |
| **COOKED_READ_DATA** | 行编辑状态, tab补全, 历史 | **无** — 行原始转发 |
| **StateMachine** | VT 解析器, Ground/Escape/CSI/OSC/DCS 状态 | **无** — 不解析VT序列 |
| **OutputStateMachineEngine** | VT输出→ITermDispatch | **无** |
| **AdaptDispatch** | ITermDispatch实现, VT→TextBuffer操作 | **无** |
| **InputStateMachineEngine** | VT输入解析→IInteractDispatch | **无** |
| **InteractDispatch** | VT输入→INPUT_RECORD | **无** |
| **ConhostInternalGetSet** | ITerminalApi实现, VT↔ScreenBuffer桥 | **无** |
| **VtIo** | VT I/O协调, 双缓冲, Overlapped | `conpty_entry.hpp` — 简化版 |
| **VtIo::Writer** | 属性→SGR, CHAR_INFO→VT, Submit | `raw_write()` — 纯字节转码 |
| **VtInputThread** | PTY读取线程, UTF-8→UTF-16, StateMachine | `on_idle()` — 非阻塞轮询等效 |
| **PtySignalInputThread** | 信号管道: resize/clear/showhide | `signal_thread_proc` — ConsoleControl转发 |
| **ApiSorter** | L1/L2/L3 API表分派 | `handle_user_defined` — 约15个API |
| **ApiDispatchers** | 每个Console API的服务端实现 | `pty_forward_handler` — 简化版 |
| **CD_IO_COMPLETE** | 异步完成: Identifier, IoStatus, Write | `read_io` / `complete_io` — 相同协议 |
| **CD_IO_DESCRIPTOR** | 消息头: Identifier, Process, Object, Function | 使用相同结构体 |
| **CONSOLE_API_MSG** | L1/L2/L3消息包装 | `io_msg` — 简化版 (body[4096]) |
| **CONSOLE_READCONSOLE_MSG** | ReadConsole参数 | 使用相同结构体 (通过 `std::memcpy`) |
| **CONSOLE_WRITECONSOLE_MSG** | WriteConsole参数 | 使用相同结构体 |

---

*本文档基于 `terminal/src/` 原始代码完整分析，覆盖所有 L1/L2/L3 API、消息结构体、核心数据结构和 VT 管线。*
*最后更新: 2026-05-18*
