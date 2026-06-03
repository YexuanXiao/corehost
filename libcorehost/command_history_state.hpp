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
    static constexpr size_t no_selection = static_cast<size_t>(-1);

    void set_capacity(size_t max_commands)
    {
        _capacity = max_commands;
        trim_to_capacity();
    }

    size_t size() const noexcept
    {
        return _commands.size();
    }

    bool empty() const noexcept
    {
        return _commands.empty();
    }

    size_t browse_index() const noexcept
    {
        return _browse_index;
    }

    const std::vector<std::u32string> &commands() const noexcept
    {
        return _commands;
    }

    void clear()
    {
        _commands.clear();
        reset_browse();
    }

    void reset_browse()
    {
        _browse_index = no_selection;
        _saved_input.clear();
    }

    void break_browse()
    {
        if (_browse_index != no_selection)
            reset_browse();
    }

    void push(std::u32string_view command)
    {
        if (command.empty())
            return;
        if (!_commands.empty() && _commands.back() == command)
            return;

        _commands.emplace_back(command);
        trim_to_capacity();
    }

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
    void trim_to_capacity()
    {
        if (_commands.size() > _capacity)
            _commands.erase(_commands.begin(),
                            _commands.begin() + static_cast<std::ptrdiff_t>(_commands.size() - _capacity));
        if (_browse_index != no_selection && _browse_index >= _commands.size())
            reset_browse();
    }

    std::vector<std::u32string> _commands;
    size_t _capacity = 50;
    size_t _browse_index = no_selection;
    std::u32string _saved_input;
};

} // namespace conpty
