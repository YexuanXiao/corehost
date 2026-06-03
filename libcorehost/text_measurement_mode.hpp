// ── conpty/text_measurement_mode.hpp ──────────────────
// 文本测量模式枚举 — cli 和 conpty 共享
//
//   console   — 传统 conhost 宽度测量 (简化 CJK/全角判断)
//   wcswidth  — libunicode::width(char32_t) (wcwidth 等效)
//   graphemes — libunicode::grapheme_cluster_width() (grapheme 分割 + emoji VS16/VS15)
#pragma once

namespace corehost::conpty
{

enum class text_measurement_mode
{
    // 传统控制台宽度模型。Console API 写入、Fill/Read 等 Win32 路径使用它，
    // 避免因为 emoji/grapheme 规则改变 legacy API 可见列数。
    console = 0,
    // 交互终端输出的默认模型。按 grapheme cluster 测量，避免 ZWJ/VS16 emoji
    // 在本地 screen_buffer 中被拆成多个可见列。
    graphemes,
    // wcwidth 兼容模型。保留给显式要求 POSIX 终端宽度的路径，不处理完整
    // grapheme cluster 合并。
    wcswidth
};

} // namespace corehost::conpty
