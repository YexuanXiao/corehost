// ── conpty/text_measurement_mode.hpp ──────────────────
// 文本测量模式枚举 — cli 和 conpty 共享
//
//   console   — 传统 conhost 宽度测量 (简化 CJK/全角判断)
//   wcswidth  — libunicode::width(char32_t) (wcwidth 等效)
//   graphemes — libunicode::grapheme_cluster_width() (grapheme 分割 + emoji VS16/VS15)
#pragma once

namespace conpty
{

enum class text_measurement_mode
{
    // console 保持传统控制台宽度估计；graphemes 是默认交互路径；wcswidth
    // 用于按 wcwidth 兼容 POSIX/终端宽度计算的场景。
    console = 0,
    graphemes,
    wcswidth
};

} // namespace conpty
