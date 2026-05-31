#pragma once
#include <string>
#include <iterator>
#include <cassert>
#include "win32/string.hpp"

namespace win32
{
// ============================================================================
// command_line_view
// ============================================================================
// 惰性解析 Windows 命令行，提供输入迭代器逐个返回参数字符串视图。
// 完全兼容 CommandLineToArgvW 的解析规则，支持：
//   - 双引号包裹包含空格的参数
//   - 反斜杠转义（\\ 和 \"）
//   - 连续双引号的三引号规则（如 "" -> 空字符串，"""" -> 单个 " 等）
//
// 设计要点：
//   1. 零拷贝：所有解析出的参数连续存储在内部的 std::wstring 缓冲区中，
//      迭代器返回的 win32::wcstring_view 指向该缓冲区，生命周期与解析器相同。
//   2. 惰性解析：构造函数不解析任何参数，迭代器首次解引用时才解析第一个参数，
//      之后每次解引用解析下一个参数。若命令行为空，迭代器直接处于结束状态。
//   3. 无状态迭代器：begin() 和 end() 返回完全相同的迭代器对象，
//      它们的相等比较依赖于父对象的 done() 状态。当 done() == false 时，
//      begin() != end()；当 done() == true 时，所有迭代器（包括 end）相等。
//   4. 输入迭代器语义：operator* 每次返回当前参数并推进解析位置，
//      因此同一迭代器连续两次解引用会得到不同的参数（允许行为）。
//      operator++ 为空操作，因为解析已在解引用中完成。
//
// 客户端使用示例：
//   command_line_view args(GetCommandLineW());
//   for (auto sv : args) {   // 遍历 argv[0], argv[1], ...
//       std::wcout << sv << L'\n';
//   }
// ============================================================================
class command_line_view
{
    win32::wcstring_view cmdline_; // 原始命令行（视图，不拥有数据）
    std::wstring buffer_;          // 连续存储所有已解析的参数（以 '\0' 分隔）
    const wchar_t *current_;       // 指向原始命令行中下一个待解析位置
    wchar_t *last_;                // 指向 buffer_ 中下一个可写入的位置
  public:
    // ------------------------------------------------------------------------
    // 输入迭代器 (C++20 std::input_iterator)
    // ------------------------------------------------------------------------
    struct iterator
    {
        using iterator_category = std::input_iterator_tag;
        using value_type = win32::wcstring_view;
        using difference_type = std::ptrdiff_t;
        using pointer = void; // 不提供 operator->
        using reference = win32::wcstring_view;

        explicit iterator(command_line_view *parent) noexcept : parent_(parent)
        {
        }

        // 解引用：返回当前参数的视图，并推进内部解析状态
        reference operator*()
        {
            assert(!parent_->done()); // 不允许在结束后解引用
            return parent_->next();
        }

        // 输入迭代器允许 ++ 为空操作（因为解引用已经推进了解析状态）
        iterator &operator++()
        {
            return *this;
        }
        iterator operator++(int)
        {
            return *this;
        }

        // 相等比较：完全依赖于父对象的 done() 状态
        // - 当 done() == false 时，所有非结束迭代器与 end 不相等
        // - 当 done() == true 时，所有迭代器（包括 end）都相等
        bool operator==(const iterator &other) const noexcept
        {
            assert(parent_ == other.parent_);
            return parent_->done();
        }

      private:
        command_line_view *parent_;
    };

    using const_iterator = iterator;

    // 构造函数：仅保留命令行视图，预分配足够缓冲区，不解析任何参数
    explicit command_line_view(win32::wcstring_view cmdline) : cmdline_(cmdline), current_(cmdline_.data())
    {
        assert(buffer_.empty());
        if (!cmdline_.empty())
        {
            // 解析出来的字符串不包含转义字符，并将相同数量的空格替换为了0，因此最长不超过原始长度
            // 注意buffer必须是std::wstring不能是std::vector，后者需要+1
            buffer_.resize(cmdline.size());
            last_ = buffer_.data();
        }
    }

    command_line_view() = delete;
    command_line_view(const command_line_view &) = delete;

    iterator begin()
    {
        return iterator(this);
    }
    iterator end()
    {
        return begin();
    }

    // 返回剩余未解析的原始命令行片段（从 current_ 到末尾）
    // 用于--惯用法，直接输出后半部分避免解析再反转义
    win32::wcstring_view remain() const noexcept
    {
        if (cmdline_.empty() || !current_)
            return {};
        const wchar_t *end = cmdline_.data() + cmdline_.size();
        if (current_ == end)
            return {};
        return win32::wcstring_view(current_, end - current_);
    }

    win32::wcstring_view next()
    {
        if (last_ == buffer_.data()) // 尚未解析任何参数，表示第一次调用
        {
            return parse_first(); // 解析并返回第一个参数
        }
        return parse_next(); // 解析后续参数
    }

    // 检查当前指针是否已到达字符串末尾
    bool done() const noexcept
    {
        const wchar_t *end = cmdline_.data() + cmdline_.size();
        return current_ == end;
    }

  private:
    // ------------------------------------------------------------------------
    // parse_first : 解析第一个参数（程序路径）
    // ------------------------------------------------------------------------
    // Windows 命令行中，第一个参数（argv[0]）遵循简化规则：
    //   - 如果以双引号 '"' 开头，则一直读取到下一个双引号（不支持反斜杠转义）。
    //   - 否则读取到空白或结尾。
    // 该函数将解析结果追加到 buffer_ 中，并返回 wstring_view 指向该字符串。
    // 同时将 current_ 移动到下一个参数的开头（跳过空白），若没有后续参数则置为结尾。
    // CommandLineToArgvW 在 cmdline 为空字符串时返回 GetModuleFileNameW 的结果，
    // 这个行为非常奇怪因此不进行模拟
    // ------------------------------------------------------------------------
    win32::wcstring_view parse_first()
    {
        const wchar_t *s = current_;
        const wchar_t *end = cmdline_.data() + cmdline_.size();

        std::size_t token_offset = last_ - buffer_.data(); // 记录参数在缓冲区中的起始偏移

        if (s != end && *s == L'"')
        {
            ++s; // 跳过开引号
            // 复制直到下一个未转义的双引号（注意：第一个参数不支持反斜杠转义）
            while (s != end && *s != L'"')
                *last_++ = *s++;
            if (s != end && *s == L'"')
                ++s; // 跳过闭引号
        }
        else
        {
            // 无引号：复制直到空白或结尾
            while (s != end && *s != L' ' && *s != L'\t')
                *last_++ = *s++;
        }
        std::size_t token_length = last_ - (buffer_.data() + token_offset);
        *last_++ = L'\0'; // 添加额外的字符串终止符，保证下一个参数之前是终止符

        // 跳过参数后的空白，使 current_ 指向下一个参数的开头（或结尾）
        while (s != end && (*s == L' ' || *s == L'\t'))
            ++s;
        current_ = s;

        return win32::wcstring_view(buffer_.data() + token_offset, token_length);
    }

    // ------------------------------------------------------------------------
    // parse_next : 解析第二个及之后的参数（遵循完整 Windows 规则）
    // ------------------------------------------------------------------------
    // 该函数实现了与 CommandLineToArgvW 完全一致的解析算法，包括：
    //   1. 反斜杠转义：
    //      - 偶数个反斜杠后跟双引号：输出一半数量的反斜杠，双引号被解释为元字符（不输出）。
    //      - 奇数个反斜杠后跟双引号：输出一半数量的反斜杠，然后输出一个文字双引号。
    //   2. 引号分组：
    //      - 双引号内的空白不会终止参数。
    //      - 参数由空白分隔，但引号内的空白被保留。
    //   3. 连续引号处理（MSDN 三引号规则）：
    //      - 两个连续双引号（""）被解释为一个文字双引号。
    //      - 三个连续双引号（"""）产生一个双引号并恢复引号状态（进/出引号段）。
    //
    // 注意：调用时 current_ 已经保证不指向空白字符（由 parse_first 或上一次 parse_next 保证），
    //       因此函数开头无需再跳过空白。
    // ------------------------------------------------------------------------
    win32::wcstring_view parse_next()
    {
        const wchar_t *end = cmdline_.data() + cmdline_.size();

        // 检查是否已到达末尾（无更多参数）
        if (current_ == end)
        {
            return {};
        }

        std::size_t token_offset = last_ - buffer_.data(); // 新参数的起始偏移
        const wchar_t *s = current_;
        int qcount = 0; // 引号计数器（模3），跟踪当前是否在引号段内
        int bcount = 0; // 连续反斜杠计数器

        while (s != end)
        {
            wchar_t ch = *s;
            // 如果遇到空白且不在引号内，则参数结束
            if ((ch == L' ' || ch == L'\t') && qcount == 0)
                break;

            if (ch == L'\\')
            {
                // 累积反斜杠，稍后根据后续字符决定处理方式
                ++bcount;
                ++s;
                continue;
            }

            if (ch == L'"')
            {
                // 根据当前累积的反斜杠数量决定行为
                if ((bcount & 1) == 0) // 偶数个反斜杠：一半输出，引号被视为元字符
                {
                    // 输出一半数量的反斜杠
                    for (int i = 0; i != bcount / 2; ++i)
                        *last_++ = L'\\';
                    // 反转引号状态（进入或退出引号段）
                    ++qcount;
                }
                else // 奇数个反斜杠：一半输出，引号作为文字字符
                {
                    for (int i = 0; i != bcount / 2; ++i)
                        *last_++ = L'\\';
                    *last_++ = L'"';
                }
                ++s;
                bcount = 0;

                // 处理连续双引号（如 "" 和 """）
                while (s != end && *s == L'"')
                {
                    // 每两个连续双引号产生一个文字双引号
                    if (++qcount == 3)
                    {
                        *last_++ = L'"';
                        qcount = 0;
                    }
                    ++s;
                }
                if (qcount == 2)
                    qcount = 0; // 重置引号状态（奇数个引号后保持引号内）
                continue;
            }

            // 普通字符：输出之前累积的所有反斜杠，然后输出该字符
            for (int i = 0; i != bcount; ++i)
                *last_++ = L'\\';
            bcount = 0;
            *last_++ = ch;
            ++s;
        }

        // 参数末尾可能残留的反斜杠（输出全部）
        for (int i = 0; i != bcount; ++i)
            *last_++ = L'\\';

        std::size_t token_length = last_ - (buffer_.data() + token_offset);
        *last_++ = L'\0'; // 添加字符串终止符

        // 跳过参数后的空白，准备解析下一个参数
        while (s != end && (*s == L' ' || *s == L'\t'))
            ++s;
        current_ = s;

        return win32::wcstring_view(buffer_.data() + token_offset, token_length);
    }
};
} // namespace win32