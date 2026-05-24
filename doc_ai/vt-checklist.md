# VT Control Sequence Checklist

基于 `doc/vt-control-sequence.md`，对标 Windows Console Virtual Terminal Sequences 完整功能列表。

状态: ✅ 已实现 | ⬜ 待实现

---

## 1. Simple Cursor Positioning (ESC 序列，无参数，立即生效)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 1 | `ESC M` | RI | Reverse Index – 光标上移一行，保持列位置，必要时滚动 | ⬜ |
| 2 | `ESC 7` | DECSC | Save Cursor Position in Memory | ⬜ |
| 3 | `ESC 8` | DECSR | Restore Cursor Position from Memory | ⬜ |

## 2. Cursor Positioning (CSI 序列)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 4 | `ESC [ n A` | CUU | Cursor Up by n (default 1) | ⬜ |
| 5 | `ESC [ n B` | CUD | Cursor Down by n (default 1) | ⬜ |
| 6 | `ESC [ n C` | CUF | Cursor Forward (Right) by n (default 1) | ⬜ |
| 7 | `ESC [ n D` | CUB | Cursor Backward (Left) by n (default 1) | ⬜ |
| 8 | `ESC [ n E` | CNL | Cursor Next Line – 下移 n 行到行首 | ⬜ |
| 9 | `ESC [ n F` | CPL | Cursor Previous Line – 上移 n 行到行首 | ⬜ |
| 10 | `ESC [ n G` | CHA | Cursor Horizontal Absolute – 移到第 n 列 | ⬜ |
| 11 | `ESC [ n d` | VPA | Vertical Line Position Absolute – 移到第 n 行 | ⬜ |
| 12 | `ESC [ y ; x H` | CUP | Cursor Position – 移到 (x, y)，默认 (1,1) | ⬜ |
| 13 | `ESC [ y ; x f` | HVP | Horizontal Vertical Position – 同 CUP | ⬜ |
| 14 | `ESC [ s` | ANSISYSSC | Save Cursor (Ansi.sys emulation) | ⬜ |
| 15 | `ESC [ u` | ANSISYSRC | Restore Cursor (Ansi.sys emulation) | ⬜ |

## 3. Cursor Visibility

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 16 | `ESC [ ? 12 h` | ATT160 | Text Cursor Enable Blinking | ⬜ |
| 17 | `ESC [ ? 12 l` | ATT160 | Text Cursor Disable Blinking | ⬜ |
| 18 | `ESC [ ? 25 h` | DECTCEM | Text Cursor Enable Mode Show | ⬜ |
| 19 | `ESC [ ? 25 l` | DECTCEM | Text Cursor Enable Mode Hide | ⬜ |

## 4. Cursor Shape (DECSCUSR, SP q 终态)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 20 | `ESC [ 0 SP q` | DECSCUSR | Default cursor shape | ⬜ |
| 21 | `ESC [ 1 SP q` | DECSCUSR | Blinking Block | ⬜ |
| 22 | `ESC [ 2 SP q` | DECSCUSR | Steady Block | ⬜ |
| 23 | `ESC [ 3 SP q` | DECSCUSR | Blinking Underline | ⬜ |
| 24 | `ESC [ 4 SP q` | DECSCUSR | Steady Underline | ⬜ |
| 25 | `ESC [ 5 SP q` | DECSCUSR | Blinking Bar | ⬜ |
| 26 | `ESC [ 6 SP q` | DECSCUSR | Steady Bar | ⬜ |

## 5. Viewport Positioning (Scroll)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 27 | `ESC [ n S` | SU | Scroll Up by n (default 1) | ⬜ |
| 28 | `ESC [ n T` | SD | Scroll Down by n (default 1) | ⬜ |

## 6. Text Modification

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 29 | `ESC [ n @` | ICH | Insert Character – 插入 n 个空格 (default 1) | ⬜ |
| 30 | `ESC [ n P` | DCH | Delete Character – 删除 n 个字符 (default 1) | ⬜ |
| 31 | `ESC [ n X` | ECH | Erase Character – 擦除 n 个字符为空格 (default 1) | ⬜ |
| 32 | `ESC [ n L` | IL | Insert Line – 插入 n 行 (default 1) | ⬜ |
| 33 | `ESC [ n M` | DL | Delete Line – 删除 n 行 (default 1) | ⬜ |
| 34 | `ESC [ n J` | ED | Erase in Display (0=to end, 1=from beginning, 2=all) | ⬜ |
| 35 | `ESC [ n K` | EL | Erase in Line (0=to end, 1=from beginning, 2=all) | ⬜ |

## 7. Text Formatting (SGR — Set Graphics Rendition)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 36 | `ESC [ n m` | SGR | Set Graphics Rendition (0=Reset, 1=Bold, 4=Underline, 7=Negative + 颜色 30-37/40-47/90-97/100-107 + 扩展色 38;2;r;g;b / 38;5;s / 48;2;r;g;b / 48;5;s) | ⬜ |

SGR 子项（含扩展色）:

| # | 参数 | 说明 | 状态 |
|---|------|------|------|
| 36a | 0 | Default (Reset all) | ⬜ |
| 36b | 1 | Bold/Bright | ⬜ |
| 36c | 22 | No bold/bright | ⬜ |
| 36d | 4 | Underline | ⬜ |
| 36e | 24 | No underline | ⬜ |
| 36f | 7 | Negative (swap fg/bg) | ⬜ |
| 36g | 27 | Positive (no negative) | ⬜ |
| 36h | 30-37 | Foreground (Black/Red/Green/Yellow/Blue/Magenta/Cyan/White) | ⬜ |
| 36i | 38;2;r;g;b | Foreground RGB extended | ⬜ |
| 36j | 38;5;s | Foreground 256-color index | ⬜ |
| 36k | 39 | Foreground Default | ⬜ |
| 36l | 40-47 | Background (Black/Red/Green/Yellow/Blue/Magenta/Cyan/White) | ⬜ |
| 36m | 48;2;r;g;b | Background RGB extended | ⬜ |
| 36n | 48;5;s | Background 256-color index | ⬜ |
| 36o | 49 | Background Default | ⬜ |
| 36p | 90-97 | Bright Foreground (Black/Red/Green/Yellow/Blue/Magenta/Cyan/White) | ⬜ |
| 36q | 100-107 | Bright Background (Black/Red/Green/Yellow/Blue/Magenta/Cyan/White) | ⬜ |

## 8. Screen Colors (OSC 序列)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 37 | `ESC ] 4 ; i ; rgb : r / g / b ST` | — | Set palette color index i to RGB(r,g,b) | ⬜ |

## 9. Mode Changes

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 38 | `ESC =` | DECKPAM | Enable Keypad Application Mode | ⬜ |
| 39 | `ESC >` | DECKPNM | Enable Keypad Numeric Mode | ⬜ |
| 40 | `ESC [ ? 1 h` | DECCKM | Enable Cursor Keys Application Mode | ⬜ |
| 41 | `ESC [ ? 1 l` | DECCKM | Disable Cursor Keys Application Mode (Normal) | ⬜ |

## 10. Query State

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 42 | `ESC [ 6 n` | DECXCPR | Report Cursor Position → response: `ESC [ r ; c R` | ⬜ |
| 43 | `ESC [ 0 c` | DA | Device Attributes → response: `ESC [ ? 1 ; 0 c` | ⬜ |

## 11. Tabs

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 44 | `ESC H` | HTS | Horizontal Tab Set – 在当前列设置制表位 | ⬜ |
| 45 | `ESC [ n I` | CHT | Cursor Horizontal (Forward) Tab by n (default 1) | ⬜ |
| 46 | `ESC [ n Z` | CBT | Cursor Backward Tab by n (default 1) | ⬜ |
| 47 | `ESC [ 0 g` | TBC | Tab Clear (current column) | ⬜ |
| 48 | `ESC [ 3 g` | TBC | Tab Clear (all columns) | ⬜ |

## 12. Designate Character Set

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 49 | `ESC ( 0` | — | Designate Character Set – DEC Line Drawing | ⬜ |
| 50 | `ESC ( B` | — | Designate Character Set – US ASCII | ⬜ |

## 13. Scrolling Margins

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 51 | `ESC [ t ; b r` | DECSTBM | Set Scrolling Region (top/bottom, inclusive) | ⬜ |

## 14. Window Title (OSC 序列)

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 52 | `ESC ] 0 ; string ST` | — | Set Window Title (icon + title) | ⬜ |
| 53 | `ESC ] 2 ; string ST` | — | Set Window Title (title only) | ⬜ |

## 15. Alternate Screen Buffer

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 54 | `ESC [ ? 1049 h` | — | Use Alternate Screen Buffer | ⬜ |
| 55 | `ESC [ ? 1049 l` | — | Use Main Screen Buffer | ⬜ |

## 16. Window Width

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 56 | `ESC [ ? 3 h` | DECCOLM | Set Number of Columns to 132 | ⬜ |
| 57 | `ESC [ ? 3 l` | DECCOLM | Set Number of Columns to 80 | ⬜ |

## 17. Soft Reset

| # | 序列 | 缩写 | 说明 | 状态 |
|---|------|------|------|------|
| 58 | `ESC [ ! p` | DECSTR | Soft Reset – 重置部分终端属性到默认值 | ⬜ |

---

## 18. Input Sequences (Terminal → Host，按键编码)

这些序列由终端**发出**，host 端需要解析。

### Cursor Keys (Cursor Keys Mode: Normal / Application)

| # | 按键 | Normal Mode | Application Mode | 状态 |
|---|------|-------------|------------------|------|
| 59 | Up Arrow | `ESC [ A` | `ESC O A` | ⬜ |
| 60 | Down Arrow | `ESC [ B` | `ESC O B` | ⬜ |
| 61 | Right Arrow | `ESC [ C` | `ESC O C` | ⬜ |
| 62 | Left Arrow | `ESC [ D` | `ESC O D` | ⬜ |
| 63 | Home | `ESC [ H` | `ESC O H` | ⬜ |
| 64 | End | `ESC [ F` | `ESC O F` | ⬜ |

### Ctrl + Cursor Keys

| # | 按键 | 序列 (Any Mode) | 状态 |
|---|------|-----------------|------|
| 65 | Ctrl+Up | `ESC [ 1 ; 5 A` | ⬜ |
| 66 | Ctrl+Down | `ESC [ 1 ; 5 B` | ⬜ |
| 67 | Ctrl+Right | `ESC [ 1 ; 5 C` | ⬜ |
| 68 | Ctrl+Left | `ESC [ 1 ; 5 D` | ⬜ |

### Numpad & Function Keys

| # | 按键 | 序列 | 状态 |
|---|------|------|------|
| 69 | Backspace | `0x7f (DEL)` | ⬜ |
| 70 | Pause | `0x1a (SUB)` | ⬜ |
| 71 | Escape | `0x1b (ESC)` | ⬜ |
| 72 | Insert | `ESC [ 2 ~` | ⬜ |
| 73 | Delete | `ESC [ 3 ~` | ⬜ |
| 74 | Page Up | `ESC [ 5 ~` | ⬜ |
| 75 | Page Down | `ESC [ 6 ~` | ⬜ |
| 76 | F1 | `ESC O P` | ⬜ |
| 77 | F2 | `ESC O Q` | ⬜ |
| 78 | F3 | `ESC O R` | ⬜ |
| 79 | F4 | `ESC O S` | ⬜ |
| 80 | F5 | `ESC [ 15 ~` | ⬜ |
| 81 | F6 | `ESC [ 17 ~` | ⬜ |
| 82 | F7 | `ESC [ 18 ~` | ⬜ |
| 83 | F8 | `ESC [ 19 ~` | ⬜ |
| 84 | F9 | `ESC [ 20 ~` | ⬜ |
| 85 | F10 | `ESC [ 21 ~` | ⬜ |
| 86 | F11 | `ESC [ 23 ~` | ⬜ |
| 87 | F12 | `ESC [ 24 ~` | ⬜ |

### Ctrl + Special Keys

| # | 按键 | 序列 | 状态 |
|---|------|------|------|
| 88 | Ctrl+Space | `0x00 (NUL)` | ⬜ |
| 89 | Ctrl+Up | `ESC [ 1 ; 5 A` | ⬜ |
| 90 | Ctrl+Down | `ESC [ 1 ; 5 B` | ⬜ |
| 91 | Ctrl+Right | `ESC [ 1 ; 5 C` | ⬜ |
| 92 | Ctrl+Left | `ESC [ 1 ; 5 D` | ⬜ |

---

**总计: 58 条输出序列 + 34 条输入序列 = 92 条**
