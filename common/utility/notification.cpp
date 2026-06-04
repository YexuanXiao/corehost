#include "notification.hpp"
#include <windows.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>
#include "win32/registry_key.hpp"
#include "win32/string.hpp"

namespace notification
{
using namespace std::literals;
namespace winrt_xml = winrt::Windows::Data::Xml::Dom;
namespace winrt_toast = winrt::Windows::UI::Notifications;

inline constexpr auto app_user_model_id = L"CoreHost"sv;

void register_app_user_model_id()
{
    auto key = win32::registry_key{win32::create_tag, win32::predefined_key::hkcu,
                                   L"Software\\Classes\\AppUserModelId\\CoreHost", KEY_WRITE, REG_OPTION_VOLATILE};

    constexpr wchar_t display_name[] = L"CoreHost";
    auto result = ::RegSetValueExW(key.get(), L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE *>(display_name),
                                   sizeof(display_name));
    if (result != ERROR_SUCCESS)
        throw static_cast<win32::error>(result);
}

void append_text(winrt_xml::XmlDocument const &xml, winrt_xml::XmlElement const &binding, win32::wcstring_view text)
{
    auto text_element = xml.CreateElement(L"text"sv);
    text_element.InnerText(winrt::hstring{text});
    binding.AppendChild(text_element);
}

void send(win32::wcstring_view title, win32::wcstring_view body, std::span<const action> action_buttons) noexcept
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        register_app_user_model_id();

        winrt_xml::XmlDocument xml;

        auto toast = xml.CreateElement(L"toast"sv);
        toast.SetAttribute(L"duration"sv, L"short"sv);
        xml.AppendChild(toast);

        auto visual = xml.CreateElement(L"visual"sv);
        toast.AppendChild(visual);

        auto binding = xml.CreateElement(L"binding"sv);
        binding.SetAttribute(L"template"sv, L"ToastGeneric"sv);
        visual.AppendChild(binding);

        append_text(xml, binding, title);
        append_text(xml, binding, body);

        if (!action_buttons.empty())
        {
            auto actions = xml.CreateElement(L"actions"sv);
            toast.AppendChild(actions);

            for (const auto &action_button : action_buttons)
            {
                auto action = xml.CreateElement(L"action"sv);
                action.SetAttribute(L"content"sv, winrt::hstring{action_button.label});
                action.SetAttribute(L"arguments"sv, winrt::hstring{action_button.arguments});
                action.SetAttribute(L"activationType"sv, L"protocol"sv);
                actions.AppendChild(action);
            }
        }

        winrt_toast::ToastNotification toast_notification{xml};
        toast_notification.ExpiresOnReboot(true);
        winrt_toast::ToastNotificationManager::CreateToastNotifier(winrt::hstring{app_user_model_id})
            .Show(toast_notification);
    }
    catch (...)
    {
        // Notifications are best-effort diagnostics. Failure to register the
        // AppUserModelId or talk to the shell must not terminate corehost.
    }
}

} // namespace notification
