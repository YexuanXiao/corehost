#pragma once

#include <Windows.h>
#include <combaseapi.h>

#include "console.hpp"
#include "utility/log.hpp"
#include "win32/srwlock.hpp"

#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <UIAutomationClient.h>
#include <oleauto.h>

#include <winrt/base.h>

#include <algorithm>
#include <mutex>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace corehost::window
{

class uia_provider;

class uia_text_range : public winrt::implements<uia_text_range, ITextRangeProvider>
{
  public:
    uia_text_range(uia_provider *owner, std::size_t start, std::size_t end) noexcept
        : owner_{owner}, start_{start}, end_{end}
    {
    }

    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider **result) override;
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider *range, BOOL *result) override;
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider *range,
                                               TextPatternRangeEndpoint target_endpoint, int *result) override;
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value, BOOL backward,
                                            ITextRangeProvider **result) override;
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignore_case,
                                       ITextRangeProvider **result) override;
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT *value) override;
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY **rectangles) override;
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple **element) override;
    HRESULT STDMETHODCALLTYPE GetText(int max_length, BSTR *text) override;
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int *moved) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
                                                 int *moved) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *range,
                                                  TextPatternRangeEndpoint target_endpoint) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL align_to_top) override;
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY **children) override;

    [[nodiscard]] std::size_t start() const noexcept
    {
        return start_;
    }
    [[nodiscard]] std::size_t end() const noexcept
    {
        return end_;
    }

  private:
    void normalize() noexcept
    {
        if (start_ > end_)
        {
            std::swap(start_, end_);
        }
    }

    uia_provider *owner_{};
    std::size_t start_{};
    std::size_t end_{};
};

class uia_provider : public winrt::implements<uia_provider,
                                                    IRawElementProviderSimple,
                                                    IRawElementProviderFragment,
                                                    IRawElementProviderFragmentRoot,
                                                    ITextProvider>
{
  public:
    uia_provider(HWND window, console &console, win32::srwlock &console_mutex) noexcept
        : window_{window}, console_{console}, console_mutex_{console_mutex}
    {
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *options) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown **provider) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT *value) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **provider) override;

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment **fragment) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **runtime_id) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *rectangle) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **roots) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **root) override;

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment **fragment) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **fragment) override;

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **selection) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY **ranges) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple *child,
                                             ITextRangeProvider **range) override;
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider **range) override;
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider **range) override;
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection *supported) override;

    struct text_snapshot final
    {
        std::u16string text;
        std::vector<std::size_t> row_offsets;
        std::vector<std::size_t> row_lengths;
    };

    [[nodiscard]] text_snapshot snapshot();
    [[nodiscard]] std::size_t character_offset(std::size_t row, std::size_t column, const text_snapshot &snapshot) const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection_offsets(text_snapshot &snapshot);
    [[nodiscard]] winrt::com_ptr<ITextRangeProvider> make_range(std::size_t start, std::size_t end);
    void select_offsets(std::size_t start, std::size_t end);

    [[nodiscard]] HWND window() const noexcept
    {
        return window_;
    }

  private:
    HWND window_{};
    console &console_;
    win32::srwlock &console_mutex_;
};

inline HRESULT uia_text_range::Clone(ITextRangeProvider **result)
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    auto clone = owner_->make_range(start_, end_);
    clone.copy_to(result);
    return clone != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_text_range::Compare(ITextRangeProvider *range, BOOL *result)
{
    if (range == nullptr || result == nullptr)
    {
        return E_INVALIDARG;
    }
    auto *other = dynamic_cast<uia_text_range *>(range);
    if (other == nullptr)
    {
        return E_INVALIDARG;
    }
    *result = start_ == other->start_ && end_ == other->end_;
    return S_OK;
}

inline HRESULT uia_text_range::CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider *range,
                                                TextPatternRangeEndpoint target_endpoint, int *result)
{
    if (range == nullptr || result == nullptr)
    {
        return E_INVALIDARG;
    }
    auto *other = dynamic_cast<uia_text_range *>(range);
    if (other == nullptr)
    {
        return E_INVALIDARG;
    }
    const auto first = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
    const auto second = target_endpoint == TextPatternRangeEndpoint_Start ? other->start_ : other->end_;
    *result = first < second ? -1 : first > second ? 1 : 0;
    return S_OK;
}

inline HRESULT uia_text_range::ExpandToEnclosingUnit(TextUnit unit)
{
    auto snapshot = owner_->snapshot();
    if (start_ >= snapshot.text.size())
    {
        return S_OK;
    }
    if (unit == TextUnit_Line)
    {
        auto begin = snapshot.text.rfind(u'\n', start_);
        begin = begin == std::u16string::npos ? 0 : begin + 1;
        auto finish = snapshot.text.find(u'\n', end_);
        finish = finish == std::u16string::npos ? snapshot.text.size() : finish;
        start_ = begin;
        end_ = finish;
    }
    normalize();
    return S_OK;
}

inline HRESULT uia_text_range::FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider **result)
{
    if (result != nullptr)
    {
        *result = nullptr;
    }
    return S_FALSE;
}

inline HRESULT uia_text_range::FindText(BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider **result)
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (text == nullptr)
    {
        return S_FALSE;
    }
    auto snapshot = owner_->snapshot();
    std::u16string needle;
    for (auto *cursor = text; *cursor != 0; ++cursor)
    {
        needle.push_back(static_cast<char16_t>(*cursor));
    }
    if (needle.empty())
    {
        return S_FALSE;
    }
    auto haystack = snapshot.text;
    auto search = needle;
    if (ignore_case)
    {
        auto lower = [](std::u16string &value) {
            for (auto &character : value)
            {
                if (character >= u'A' && character <= u'Z')
                {
                    character = static_cast<char16_t>(character - u'A' + u'a');
                }
            }
        };
        lower(haystack);
        lower(search);
    }
    const auto found = backward ? haystack.rfind(search) : haystack.find(search);
    if (found == std::u16string::npos)
    {
        return S_FALSE;
    }
    auto found_range = owner_->make_range(found, found + search.size());
    found_range.copy_to(result);
    return found_range != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_text_range::GetAttributeValue(TEXTATTRIBUTEID, VARIANT *value)
{
    if (value == nullptr)
    {
        return E_POINTER;
    }
    VariantInit(value);
    return S_OK;
}

inline HRESULT uia_text_range::GetBoundingRectangles(SAFEARRAY **rectangles)
{
    if (rectangles == nullptr)
    {
        return E_POINTER;
    }
    *rectangles = SafeArrayCreateVector(VT_R8, 0, 0);
    return *rectangles != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_text_range::GetEnclosingElement(IRawElementProviderSimple **element)
{
    if (element == nullptr)
    {
        return E_POINTER;
    }
    return owner_->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void **>(element));
}

inline HRESULT uia_text_range::GetText(int max_length, BSTR *text)
{
    if (text == nullptr)
    {
        return E_POINTER;
    }
    *text = nullptr;
    auto snapshot = owner_->snapshot();
    const auto begin = std::min(start_, snapshot.text.size());
    auto count = std::min(end_, snapshot.text.size()) - begin;
    if (max_length >= 0)
    {
        count = std::min<std::size_t>(count, static_cast<std::size_t>(max_length));
    }
    *text = SysAllocStringLen(reinterpret_cast<const OLECHAR *>(snapshot.text.data() + begin),
                              static_cast<UINT>(count));
    return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_text_range::Move(TextUnit unit, int count, int *moved)
{
    auto snapshot = owner_->snapshot();
    const auto length = snapshot.text.size();
    std::size_t delta = 0;
    if (unit == TextUnit_Character)
    {
        delta = static_cast<std::size_t>(std::abs(count));
    }
    else if (unit == TextUnit_Line)
    {
        while (delta < length && snapshot.text[delta] != u'\n')
        {
            ++delta;
        }
        if (delta < length)
        {
            ++delta;
        }
        delta *= static_cast<std::size_t>(std::abs(count));
    }
    else
    {
        delta = static_cast<std::size_t>(std::abs(count));
    }
    delta = std::min(delta, length);
    const auto amount = count < 0 ? -static_cast<std::ptrdiff_t>(delta) : static_cast<std::ptrdiff_t>(delta);
    const auto new_start = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(start_) + amount, 0,
                                                       static_cast<std::ptrdiff_t>(length));
    const auto new_end = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(end_) + amount, 0,
                                                     static_cast<std::ptrdiff_t>(length));
    const auto actual = std::abs(new_start - static_cast<std::ptrdiff_t>(start_));
    start_ = static_cast<std::size_t>(new_start);
    end_ = static_cast<std::size_t>(new_end);
    normalize();
    if (moved != nullptr)
    {
        *moved = count < 0 ? -static_cast<int>(actual) : static_cast<int>(actual);
    }
    return S_OK;
}

inline HRESULT uia_text_range::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
                                                  int *moved)
{
    if (moved != nullptr)
    {
        *moved = 0;
    }
    auto snapshot = owner_->snapshot();
    const auto length = snapshot.text.size();
    auto &value = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
    const auto delta = static_cast<std::ptrdiff_t>(std::abs(count));
    const auto amount = count < 0 ? -delta : delta;
    const auto next = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(value) + amount, 0,
                                                 static_cast<std::ptrdiff_t>(length));
    if (unit == TextUnit_Line && next != 0 && next != length)
    {
        const auto found = snapshot.text.find(amount > 0 ? u'\n' : u'\n', static_cast<std::size_t>(next));
        if (found != std::u16string::npos)
        {
            value = amount > 0 ? found + 1 : found;
            if (moved != nullptr)
            {
                *moved = count < 0 ? -1 : 1;
            }
            normalize();
            return S_OK;
        }
    }
    if (moved != nullptr)
    {
        *moved = static_cast<int>(next - static_cast<std::ptrdiff_t>(value));
    }
    value = static_cast<std::size_t>(next);
    normalize();
    return S_OK;
}

inline HRESULT uia_text_range::MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *range,
                                                   TextPatternRangeEndpoint target_endpoint)
{
    if (range == nullptr)
    {
        return E_INVALIDARG;
    }
    auto *other = dynamic_cast<uia_text_range *>(range);
    if (other == nullptr)
    {
        return E_INVALIDARG;
    }
    auto &value = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
    value = target_endpoint == TextPatternRangeEndpoint_Start ? other->start_ : other->end_;
    normalize();
    return S_OK;
}

inline HRESULT uia_text_range::Select()
{
    owner_->select_offsets(start_, end_);
    return S_OK;
}

inline HRESULT uia_text_range::AddToSelection()
{
    return Select();
}

inline HRESULT uia_text_range::RemoveFromSelection()
{
    return S_OK;
}

inline HRESULT uia_text_range::ScrollIntoView(BOOL)
{
    return S_OK;
}

inline HRESULT uia_text_range::GetChildren(SAFEARRAY **children)
{
    if (children == nullptr)
    {
        return E_POINTER;
    }
    *children = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *children != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_provider::get_ProviderOptions(ProviderOptions *options)
{
    if (options == nullptr)
    {
        return E_POINTER;
    }
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

inline HRESULT uia_provider::GetPatternProvider(PATTERNID pattern, IUnknown **provider)
{
    if (provider == nullptr)
    {
        return E_POINTER;
    }
    *provider = nullptr;
    if (pattern == UIA_TextPatternId)
    {
        *provider = static_cast<ITextProvider *>(this);
        AddRef();
        return S_OK;
    }
    return S_OK;
}

inline HRESULT uia_provider::GetPropertyValue(PROPERTYID property, VARIANT *value)
{
    if (value == nullptr)
    {
        return E_POINTER;
    }
    VariantInit(value);
    switch (property)
    {
    case UIA_NamePropertyId:
    {
        wchar_t title[512]{};
        const auto length = ::GetWindowTextW(window_, title, static_cast<int>(std::size(title)));
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocStringLen(title, static_cast<UINT>(length));
        return S_OK;
    }
    case UIA_ControlTypePropertyId:
        value->vt = VT_I4;
        value->lVal = UIA_DocumentControlTypeId;
        return S_OK;
    case UIA_IsContentElementPropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsKeyboardFocusablePropertyId:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
        return S_OK;
    case UIA_HasKeyboardFocusPropertyId:
        value->vt = VT_BOOL;
        value->boolVal = ::GetFocus() == window_ ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    case UIA_FrameworkIdPropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(L"corehost");
        return S_OK;
    case UIA_ClassNamePropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(L"corehost console");
        return S_OK;
    case UIA_NativeWindowHandlePropertyId:
        value->vt = VT_I4;
        value->lVal = static_cast<LONG>(reinterpret_cast<INT_PTR>(window_));
        return S_OK;
    default:
        return S_OK;
    }
}

inline HRESULT uia_provider::get_HostRawElementProvider(IRawElementProviderSimple **provider)
{
    if (provider == nullptr)
    {
        return E_POINTER;
    }
    return ::UiaHostProviderFromHwnd(window_, provider);
}

inline HRESULT uia_provider::Navigate(NavigateDirection, IRawElementProviderFragment **fragment)
{
    if (fragment == nullptr)
    {
        return E_POINTER;
    }
    *fragment = nullptr;
    return S_OK;
}

inline HRESULT uia_provider::GetRuntimeId(SAFEARRAY **runtime_id)
{
    if (runtime_id == nullptr)
    {
        return E_POINTER;
    }
    *runtime_id = SafeArrayCreateVector(VT_I4, 0, 2);
    if (*runtime_id == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    LONG values[2]{UiaAppendRuntimeId, static_cast<LONG>(reinterpret_cast<INT_PTR>(window_))};
    for (LONG index = 0; index < 2; ++index)
    {
        SafeArrayPutElement(*runtime_id, &index, &values[index]);
    }
    return S_OK;
}

inline HRESULT uia_provider::get_BoundingRectangle(UiaRect *rectangle)
{
    if (rectangle == nullptr)
    {
        return E_POINTER;
    }
    RECT bounds{};
    if (::GetWindowRect(window_, &bounds) == FALSE)
    {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    *rectangle = UiaRect{static_cast<double>(bounds.left), static_cast<double>(bounds.top),
                         static_cast<double>(bounds.right - bounds.left),
                         static_cast<double>(bounds.bottom - bounds.top)};
    return S_OK;
}

inline HRESULT uia_provider::GetEmbeddedFragmentRoots(SAFEARRAY **roots)
{
    if (roots == nullptr)
    {
        return E_POINTER;
    }
    *roots = nullptr;
    return S_OK;
}

inline HRESULT uia_provider::SetFocus()
{
    ::SetForegroundWindow(window_);
    ::SetFocus(window_);
    return S_OK;
}

inline HRESULT uia_provider::get_FragmentRoot(IRawElementProviderFragmentRoot **root)
{
    if (root == nullptr)
    {
        return E_POINTER;
    }
    *root = static_cast<IRawElementProviderFragmentRoot *>(this);
    AddRef();
    return S_OK;
}

inline HRESULT uia_provider::ElementProviderFromPoint(double, double, IRawElementProviderFragment **fragment)
{
    if (fragment == nullptr)
    {
        return E_POINTER;
    }
    *fragment = static_cast<IRawElementProviderFragment *>(this);
    AddRef();
    return S_OK;
}

inline HRESULT uia_provider::GetFocus(IRawElementProviderFragment **fragment)
{
    if (fragment == nullptr)
    {
        return E_POINTER;
    }
    *fragment = static_cast<IRawElementProviderFragment *>(this);
    AddRef();
    return S_OK;
}

inline HRESULT uia_provider::GetSelection(SAFEARRAY **selection)
{
    if (selection == nullptr)
    {
        return E_POINTER;
    }
    *selection = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (*selection == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    auto snapshot = this->snapshot();
    const auto [start, end] = selection_offsets(snapshot);
    auto range = make_range(start, end);
    if (range == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    SafeArrayPutElement(*selection, &index, static_cast<ITextRangeProvider *>(range.get()));
    range.detach();
    return S_OK;
}

inline HRESULT uia_provider::GetVisibleRanges(SAFEARRAY **ranges)
{
    if (ranges == nullptr)
    {
        return E_POINTER;
    }
    *ranges = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (*ranges == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    auto snapshot = this->snapshot();
    auto range = make_range(0, snapshot.text.size());
    if (range == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    SafeArrayPutElement(*ranges, &index, static_cast<ITextRangeProvider *>(range.get()));
    range.detach();
    return S_OK;
}

inline HRESULT uia_provider::RangeFromChild(IRawElementProviderSimple *, ITextRangeProvider **range)
{
    return get_DocumentRange(range);
}

inline HRESULT uia_provider::RangeFromPoint(UiaPoint, ITextRangeProvider **range)
{
    return get_DocumentRange(range);
}

inline HRESULT uia_provider::get_DocumentRange(ITextRangeProvider **range)
{
    if (range == nullptr)
    {
        return E_POINTER;
    }
    auto snapshot = this->snapshot();
    auto document = make_range(0, snapshot.text.size());
    document.copy_to(range);
    return document != nullptr ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT uia_provider::get_SupportedTextSelection(SupportedTextSelection *supported)
{
    if (supported == nullptr)
    {
        return E_POINTER;
    }
    *supported = SupportedTextSelection_Single;
    return S_OK;
}

inline uia_provider::text_snapshot uia_provider::snapshot()
{
    std::unique_lock<win32::srwlock> lock{console_mutex_};
    text_snapshot result;
    const auto rows = console_.history_size() + console_.buffer().height();
    const auto width = console_.buffer().width();
    result.row_offsets.reserve(rows);
    result.row_lengths.reserve(rows);
    for (std::size_t y = 0; y < rows; ++y)
    {
        result.row_offsets.push_back(result.text.size());
        std::u16string line;
        for (std::size_t x = 0; x < width; ++x)
        {
            const auto cell = console_.cell_at_virtual(y, x);
            if (cell.is_continuation())
            {
                continue;
            }
            if (cell.is_blank())
            {
                line.push_back(u' ');
                continue;
            }
            const auto cluster = console_.cluster_at_virtual(y, cell);
            if (cluster.empty())
            {
                line.push_back(cell.primary());
            }
            else
            {
                line.append(cluster.begin(), cluster.end());
            }
        }
        while (!line.empty() && line.back() == u' ')
        {
            line.pop_back();
        }
        result.row_lengths.push_back(line.size());
        result.text.append(line);
        if (y + 1 < rows)
        {
            result.text.append(u"\r\n");
        }
    }
    LOG2("uia snapshot history=%zu height=%zu width=%zu text=%zu sample=%04x", console_.history_size(),
         console_.buffer().height(), width, result.text.size(), console_.buffer().at(0, 0).primary());
    while (result.row_offsets.size() > 1 && result.row_lengths.back() == 0)
    {
        result.row_offsets.pop_back();
        result.row_lengths.pop_back();
        if (result.text.size() >= 2)
        {
            result.text.resize(result.text.size() - 2);
        }
    }
    return result;
}

inline std::size_t uia_provider::character_offset(std::size_t row, std::size_t column,
                                                  const text_snapshot &snapshot) const
{
    if (snapshot.row_offsets.empty())
    {
        return 0;
    }
    row = std::min(row, snapshot.row_offsets.size() - 1);
    return snapshot.row_offsets[row] + std::min(column, snapshot.row_lengths[row]);
}

inline std::pair<std::size_t, std::size_t> uia_provider::selection_offsets(text_snapshot &snapshot)
{
    std::unique_lock<win32::srwlock> lock{console_mutex_};
    const auto selection = console_.selection();
    if (selection.active)
    {
        auto start = character_offset(selection.anchor_y, selection.anchor_x, snapshot);
        auto end = character_offset(selection.caret_y, selection.caret_x, snapshot);
        if (start > end)
        {
            std::swap(start, end);
        }
        return {start, end};
    }
    const auto cursor = console_.state().cursor();
    const auto offset = character_offset(console_.history_size() + cursor.y, cursor.x, snapshot);
    return {offset, offset};
}

inline winrt::com_ptr<ITextRangeProvider> uia_provider::make_range(std::size_t start, std::size_t end)
{
    return winrt::make<uia_text_range>(this, start, end);
}

inline void uia_provider::select_offsets(std::size_t start, std::size_t end)
{
    auto snapshot = this->snapshot();
    auto locate = [&](std::size_t offset) {
        std::size_t row = 0;
        while (row + 1 < snapshot.row_offsets.size() && snapshot.row_offsets[row + 1] <= offset)
        {
            ++row;
        }
        const auto column = std::min(offset, snapshot.row_offsets[row] + snapshot.row_lengths[row]) -
                            snapshot.row_offsets[row];
        return std::pair{row, column};
    };
    const auto [anchor_y, anchor_x] = locate(start);
    const auto [caret_y, caret_x] = locate(end);
    std::unique_lock<win32::srwlock> lock{console_mutex_};
    console_.set_selection(anchor_x, anchor_y, caret_x, caret_y);
}

} // namespace corehost::window
