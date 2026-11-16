from __future__ import annotations

import csv
import statistics
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
RAW = Path(r"C:\1\bench-results\compare-workers-3x.txt")
OUT_CSV = ROOT / "benchmark_openconsole_vs_corehost2.csv"
OUT_PNG = ROOT / "benchmark_openconsole_vs_corehost2.png"

SCENARIOS = [
    ("large-mixed-vt-cjk-output", "large-mixed"),
    ("long-line-three-vt-output", "long-line"),
    ("powershell-type-realistic-build-output", "powershell-type"),
    ("sgr-plain", "sgr-plain"),
    ("cjk-terminal-input", "cjk-terminal-input"),
]

rows: list[dict] = []
for line in RAW.read_text(encoding="utf-8").splitlines():
    if not line.startswith("RESULT\t"):
        continue
    _, host, scenario, status, nbytes, ms = line.split("\t")
    rows.append(
        {
            "host": host,
            "scenario": scenario,
            "status": int(status),
            "bytes": int(nbytes),
            "ms": float(ms),
        }
    )

by_scenario: dict[str, dict[str, list[dict]]] = {}
for row in rows:
    by_scenario.setdefault(row["scenario"], {}).setdefault(row["host"], []).append(row)

summary: list[dict] = []
for scenario, hosts in by_scenario.items():
    oc = hosts.get("OpenConsole", [])
    ch = hosts.get("corehost2", [])
    if not oc or not ch:
        continue
    oc_ms = [r["ms"] for r in oc]
    ch_ms = [r["ms"] for r in ch]
    summary.append(
        {
            "scenario": scenario,
            "oc_runs": len(oc_ms),
            "ch_runs": len(ch_ms),
            "oc_avg_ms": statistics.mean(oc_ms),
            "oc_min_ms": min(oc_ms),
            "oc_max_ms": max(oc_ms),
            "ch_avg_ms": statistics.mean(ch_ms),
            "ch_min_ms": min(ch_ms),
            "ch_max_ms": max(ch_ms),
            "ratio_ch_over_oc": statistics.mean(ch_ms) / statistics.mean(oc_ms),
            "oc_bytes": oc[0]["bytes"],
            "ch_bytes": ch[0]["bytes"],
        }
    )

summary.sort(key=lambda r: r["scenario"])
fieldnames = list(summary[0].keys())
with OUT_CSV.open("w", newline="", encoding="utf-8-sig") as handle:
    writer = csv.DictWriter(handle, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(summary)

by_name = {r["scenario"]: r for r in summary}
sgr = [r for r in summary if r["scenario"].startswith("sgr-")]
non_cjk = [r for r in summary if r["scenario"] != "cjk-terminal-input"]
sgr_oc = statistics.mean(r["oc_avg_ms"] for r in sgr)
sgr_ch = statistics.mean(r["ch_avg_ms"] for r in sgr)
all_oc = statistics.mean(r["oc_avg_ms"] for r in non_cjk)
all_ch = statistics.mean(r["ch_avg_ms"] for r in non_cjk)

W, H = 1600, 1040
bg = "#F7F8FA"
fg = "#17212B"
muted = "#5D6B7A"
oc_color = "#7C8FA3"
ch_color = "#0F9D8A"
grid = "#DDE3E9"

image = Image.new("RGB", (W, H), bg)
draw = ImageDraw.Draw(image)
font_title = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 40)
font_sub = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 22)
font_panel = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 25)
font_label = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 19)
font_small = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 16)
font_num = ImageFont.truetype(r"C:\Windows\Fonts\segoeui.ttf", 18)


def text(x, y, value, font=font_label, color=fg):
    draw.text((x, y), value, font=font, fill=color)


def bar_panel(x0, y0, w, h, title, items, max_ms, note=None):
    text(x0, y0, title, font_panel)
    if note:
        text(x0, y0 + 32, note, font_small, muted)
    chart_top = y0 + 76
    chart_bottom = y0 + h - 26
    chart_left = x0 + 160
    chart_right = x0 + w - 28
    # grid
    for i in range(6):
        gx = chart_left + (chart_right - chart_left) * i / 5
        draw.line((gx, chart_top, gx, chart_bottom), fill=grid, width=1)
        value = max_ms * i / 5
        label = f"{value:.0f}" if max_ms >= 50 else f"{value:.1f}"
        draw.text((gx - 10, chart_bottom + 4), label, font=font_small, fill=muted)
    row_gap = (chart_bottom - chart_top) / max(len(items), 1)
    for idx, item in enumerate(items):
        name, oc, ch = item
        cy = chart_top + row_gap * idx + row_gap / 2
        label_w = 140
        draw.text((x0, cy - 23), name, font=font_label, fill=fg)
        bh = 17
        for offset, value, color in ((0, oc, oc_color), (24, ch, ch_color)):
            bx = chart_left + (chart_right - chart_left) * value / max_ms
            draw.rounded_rectangle(
                (chart_left, cy - 16 + offset, max(chart_left + 2, bx), cy - 16 + offset + bh),
                radius=4,
                fill=color,
            )
            label = f"{value:.1f} ms"
            draw.text((max(chart_left + 6, bx + 8), cy - 19 + offset), label, font=font_num, fill=fg)


text(60, 44, "corehost2 vs OpenConsole", font_title)
text(60, 96, "conpty_bench, 3 runs average, worker mode", font_sub, muted)

# legend
draw.rounded_rectangle((1130, 56, 1150, 76), radius=4, fill=oc_color)
text(1160, 54, "OpenConsole", font_label, fg)
draw.rounded_rectangle((1320, 56, 1340, 76), radius=4, fill=ch_color)
text(1350, 54, "corehost2", font_label, fg)

main_items = []
for scenario, label in SCENARIOS[:4]:
    r = by_name[scenario]
    main_items.append((label, r["oc_avg_ms"], r["ch_avg_ms"]))
bar_panel(60, 150, 720, 390, "主场景（ms，越低越好）", main_items, 1000)

cjk = by_name["cjk-terminal-input"]
bar_panel(
    830,
    150,
    710,
    390,
    "CJK 输入（ms；注意横轴量级）",
    [("cjk-terminal", cjk["oc_avg_ms"], cjk["ch_avg_ms"])],
    cjk["oc_avg_ms"] * 1.08,
    f"OpenConsole {cjk['oc_avg_ms']:.1f} ms  /  corehost2 {cjk['ch_avg_ms']:.1f} ms  =>  {cjk['oc_avg_ms'] / cjk['ch_avg_ms']:.0f}x faster",
)

bar_panel(
    60,
    580,
    720,
    250,
    "SGR 20 个场景均值（ms）",
    [("sgr average", sgr_oc, sgr_ch)],
    40,
    f"{sgr_oc:.1f} ms vs {sgr_ch:.1f} ms  (ratio {sgr_ch / sgr_oc:.3f})",
)

bar_panel(
    830,
    580,
    710,
    250,
    "全部非 CJK 场景均值（ms）",
    [("scenario mean", all_oc, all_ch)],
    110,
    f"{all_oc:.1f} ms vs {all_ch:.1f} ms  (ratio {all_ch / all_oc:.3f})",
)

text(60, 900, "说明", font_panel)
text(
    60,
    938,
    "耗时越低越好；ratio = corehost2 / OpenConsole。CJK 的 corehost2 输出字节数更高，是因为 line echo 统一走 renderer 后增加了 CUP 序列。",
    font_small,
    muted,
)
text(
    60,
    968,
    "OpenConsole 版本 1.26.2611.09003；corehost2 Release 无日志构建。powershell 场景两边的输出字节数不一致，表中只比较耗时，工作量等价性需单独复核。",
    font_small,
    muted,
)

image.save(OUT_PNG)
print(OUT_PNG)
print(OUT_CSV)
print(f"sgr: {sgr_oc:.2f} vs {sgr_ch:.2f}, ratio {sgr_ch / sgr_oc:.3f}")
print(f"non-cjk mean: {all_oc:.2f} vs {all_ch:.2f}, ratio {all_ch / all_oc:.3f}")
