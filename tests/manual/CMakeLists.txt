# manual/CMakeLists.txt — 手动验证测试程序
# 每个测试生成独立 .exe，不在 CTest 中注册
# 用法：在 ConPTY (corehost.exe) 下运行，肉眼观察输出
cmake_minimum_required(VERSION 3.30)

# ── 公共头文件 manual_common.hpp ──
# 所有 manual 目标自动包含 manual/ 目录
add_library(manual_common INTERFACE)
target_include_directories(manual_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(manual_common INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(manual_common INTERFACE /utf-8)
endif()

# ── add_manual_test(name source) ──
# 每个手动测试生成独立 .exe，继承全局 CMAKE_RUNTIME_OUTPUT_DIRECTORY
macro(add_manual_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE manual_common)
endmacro()

# ══════════════ API 测试 (api_001 ~ api_015) ══════════
add_manual_test(api_001_write_console          api_001_write_console.cpp)
add_manual_test(api_002_screen_buffer_info     api_002_screen_buffer_info.cpp)
add_manual_test(api_003_cursor_position        api_003_cursor_position.cpp)
add_manual_test(api_004_fill_character         api_004_fill_character.cpp)
add_manual_test(api_005_fill_attribute         api_005_fill_attribute.cpp)
add_manual_test(api_006_write_output_char      api_006_write_output_char.cpp)
add_manual_test(api_007_write_output_attr      api_007_write_output_attr.cpp)
add_manual_test(api_008_scroll                 api_008_scroll.cpp)
add_manual_test(api_009_text_attr              api_009_text_attr.cpp)
add_manual_test(api_010_cursor_info            api_010_cursor_info.cpp)
add_manual_test(api_011_title                  api_011_title.cpp)
add_manual_test(api_012_console_mode           api_012_console_mode.cpp)
add_manual_test(api_013_codepage               api_013_codepage.cpp)
add_manual_test(api_014_largest_window         api_014_largest_window.cpp)
add_manual_test(api_015_read_console           api_015_read_console.cpp)

# ══════════════ VT 测试 (vt_001 ~ vt_018) ════════════
add_manual_test(vt_001_cursor                  vt_001_cursor.cpp)
add_manual_test(vt_002_sgr_16color             vt_002_sgr_16color.cpp)
add_manual_test(vt_003_sgr_extended            vt_003_sgr_extended.cpp)
add_manual_test(vt_004_sgr_attrs               vt_004_sgr_attrs.cpp)
add_manual_test(vt_005_ed                      vt_005_ed.cpp)
add_manual_test(vt_006_el                      vt_006_el.cpp)
add_manual_test(vt_007_decsc_decrc             vt_007_decsc_decrc.cpp)
add_manual_test(vt_008_cursor_style            vt_008_cursor_style.cpp)
add_manual_test(vt_009_su_sd_il_dl             vt_009_su_sd_il_dl.cpp)
add_manual_test(vt_010_osc                     vt_010_osc.cpp)
add_manual_test(vt_011_rep_dch_ich             vt_011_rep_dch_ich.cpp)
add_manual_test(vt_012_da_dsr                  vt_012_da_dsr.cpp)
add_manual_test(vt_013_decawm                  vt_013_decawm.cpp)
add_manual_test(vt_014_charset                 vt_014_charset.cpp)
add_manual_test(vt_015_alt_buffer              vt_015_alt_buffer.cpp)
add_manual_test(vt_016_ris                     vt_016_ris.cpp)
add_manual_test(vt_017_resize                  vt_017_resize.cpp)
add_manual_test(vt_018_comprehensive           vt_018_comprehensive.cpp)

# ══════════════ Unicode 测试 (uni_001 ~ uni_018) ══════
add_manual_test(uni_001_bmp                    uni_001_bmp.cpp)
add_manual_test(uni_002_combining              uni_002_combining.cpp)
add_manual_test(uni_003_emoji_basic            uni_003_emoji_basic.cpp)
add_manual_test(uni_004_emoji_flags            uni_004_emoji_flags.cpp)
add_manual_test(uni_005_rtl                    uni_005_rtl.cpp)
add_manual_test(uni_006_cjk                    uni_006_cjk.cpp)
add_manual_test(uni_007_zero_width             uni_007_zero_width.cpp)
add_manual_test(uni_008_tab_unicode            uni_008_tab_unicode.cpp)
add_manual_test(uni_009_math                   uni_009_math.cpp)
add_manual_test(uni_010_vs                     uni_010_vs.cpp)
add_manual_test(uni_011_line_break             uni_011_line_break.cpp)
add_manual_test(uni_012_control_pictures       uni_012_control_pictures.cpp)
add_manual_test(uni_013_spaces                 uni_013_spaces.cpp)
add_manual_test(uni_014_ancient                uni_014_ancient.cpp)
add_manual_test(uni_015_supplementary          uni_015_supplementary.cpp)
add_manual_test(uni_016_ligatures              uni_016_ligatures.cpp)
add_manual_test(uni_017_mixed_emoji_text       uni_017_mixed_emoji_text.cpp)
add_manual_test(uni_018_long_text_wrap         uni_018_long_text_wrap.cpp)
