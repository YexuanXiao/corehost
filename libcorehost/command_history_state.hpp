#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace conpty
{

class command_history_state
{
  public:
    // no_selection 表示当前没有处于上下键浏览历史的状态。
    static constexpr size_t no_selection = static_cast<size_t>(-1);

    // 设置 DOSKEY 历史容量；容量缩小时立即丢弃最旧命令。
    void set_capacity(size_t max_commands)
    {
        _capacity = max_commands;
        trim_to_capacity();
    }

    // 返回当前可枚举给 GetConsoleCommandHistory 的命令数量。
    size_t size() const noexcept
    {
        return _commands.size();
    }

    // true 表示没有可浏览或可导出的历史命令。
    bool empty() const noexcept
    {
        return _commands.empty();
    }

    // 返回当前浏览到的历史下标；no_selection 表示未浏览。
    size_t browse_index() const noexcept
    {
        return _browse_index;
    }

    // 返回 API 序列化使用的命令列表；调用方只读，不拥有返回引用。
    const std::vector<std::u32string> &commands() const noexcept
    {
        return _commands;
    }

    // 清空命令历史，并终止正在进行的上下键浏览。
    void clear()
    {
        _commands.clear();
        reset_browse();
    }

    // 退出历史浏览；保留命令列表但丢弃用于恢复当前输入的临时文本。
    void reset_browse()
    {
        _browse_index = no_selection;
        _saved_input.clear();
    }

    // 当前输入被普通编辑修改后调用，避免后续 Down 继续恢复旧输入。
    void break_browse()
    {
        if (_browse_index != no_selection)
            reset_browse();
    }

    // 把完成的非空命令加入历史；连续重复命令不会重复保存。
    void push(std::u32string_view command)
    {
        if (command.empty())
            return;
        if (!_commands.empty() && _commands.back() == command)
            return;

        _commands.emplace_back(command);
        trim_to_capacity();
    }

    // 上键浏览历史。首次浏览时保存 current_input，便于 BrowseDown 恢复。
    bool browse_up(std::u32string_view current_input, std::u32string &selected_command)
    {
        if (_commands.empty())
            return false;

        if (_browse_index == no_selection)
        {
            _saved_input.assign(current_input.begin(), current_input.end());
            _browse_index = _commands.size() - 1;
        }
        else if (_browse_index > 0)
        {
            --_browse_index;
        }

        selected_command.clear();
        selected_command.append(_commands[_browse_index]);
        return true;
    }

    // 下键浏览历史；越过最新历史项时恢复首次 BrowseUp 前的输入。
    bool browse_down(std::u32string &selected_command)
    {
        if (_browse_index == no_selection)
            return false;

        if (_browse_index + 1 < _commands.size())
        {
            ++_browse_index;
            selected_command.clear();
            selected_command.append(_commands[_browse_index]);
        }
        else
        {
            selected_command.clear();
            selected_command.append(_saved_input);
            reset_browse();
        }
        return true;
    }

  private:
    // 保持 _commands.size() <= _capacity，并修正已经失效的浏览下标。
    void trim_to_capacity()
    {
        if (_commands.size() > _capacity)
            _commands.erase(_commands.begin(),
                            _commands.begin() + static_cast<std::ptrdiff_t>(_commands.size() - _capacity));
        if (_browse_index != no_selection && _browse_index >= _commands.size())
            reset_browse();
    }

    // 已提交命令，按时间从旧到新排序。
    std::vector<std::u32string> _commands;
    // 最大保存命令数，来自 Console history API。
    size_t _capacity = 50;
    // 当前浏览下标；no_selection 表示没有浏览状态。
    size_t _browse_index = no_selection;
    // BrowseUp 前的用户输入，BrowseDown 越过末尾时恢复。
    std::u32string _saved_input;
};

} // namespace conpty
