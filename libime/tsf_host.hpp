#pragma once

// Minimal TSF client for the windowed console. The context is a transitory
// TSF document. Composition ranges are inspected through a read/write edit
// session and finalized text is forwarded to the console input queue.

#include <Windows.h>
#include <msctf.h>
#include <winrt/base.h>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::window
{
    class tsf_host final : public ITfContextOwner, public ITfContextOwnerCompositionSink, public ITfTextEditSink
    {
      public:
        using text_sink = std::function<void(std::wstring_view)>;
        using rect_provider = std::function<RECT()>;

        tsf_host(HWND window, text_sink commit, text_sink composition, rect_provider caret)
            : window_{window}, commit_{std::move(commit)}, composition_{std::move(composition)},
              caret_{std::move(caret)}, edit_session_{*this}
        {
        }

        ~tsf_host() = default;

        HRESULT initialize()
        {
            auto hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            com_initialized_ = SUCCEEDED(hr);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            {
                return hr;
            }

            hr = ::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(thread_mgr_.put()));
            if (FAILED(hr))
            {
                return hr;
            }

            thread_ex_ = thread_mgr_.try_as<ITfThreadMgrEx>();
            if (thread_ex_)
            {
                hr = thread_ex_->ActivateEx(&client_id_, TF_TMAE_NOACTIVATETIP | TF_TMAE_NOACTIVATEKEYBOARDLAYOUT | TF_TMAE_CONSOLE);
            }
            else
            {
                hr = thread_mgr_->Activate(&client_id_);
            }
            if (FAILED(hr))
            {
                return hr;
            }

            hr = thread_mgr_->CreateDocumentMgr(document_mgr_.put());
            if (FAILED(hr))
            {
                return hr;
            }

            hr = document_mgr_->CreateContext(client_id_, 0, static_cast<ITfContextOwnerCompositionSink *>(this),
                                              context_.put(), &edit_cookie_);
            if (FAILED(hr))
            {
                return hr;
            }

            if (auto source = context_.try_as<ITfSource>(); source)
            {
                const auto owner_advise =
                    source->AdviseSink(IID_ITfContextOwner, static_cast<ITfContextOwner *>(this), &owner_cookie_);
                const auto edit_advise =
                    source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this), &edit_sink_cookie_);
                LOG2("tsf advise owner=%08x edit=%08x", static_cast<unsigned>(owner_advise),
                     static_cast<unsigned>(edit_advise));
            }

            hr = document_mgr_->Push(context_.get());
            if (FAILED(hr))
            {
                return hr;
            }

            if (thread_ex_)
            {
                winrt::com_ptr<ITfDocumentMgr> previous;
                const auto associate = thread_ex_->AssociateFocus(window_, document_mgr_.get(), previous.put());
                LOG2("tsf associate focus hr=%08x", static_cast<unsigned>(associate));
            }

            LOG2("tsf initialized client=%u context=%p", static_cast<unsigned>(client_id_), context_.get());
            (void)thread_mgr_->SetFocus(document_mgr_.get());
            return S_OK;
        }

        void shutdown() noexcept
        {
            if (thread_mgr_)
            {
                (void)thread_mgr_->SetFocus(nullptr);
            }
            if (document_mgr_ && context_)
            {
                (void)document_mgr_->Pop(TF_POPF_ALL);
            }
            if (context_ && owner_cookie_ != TF_INVALID_COOKIE)
            {
                if (auto source = context_.try_as<ITfSource>(); source)
                {
                    (void)source->UnadviseSink(owner_cookie_);
                }
                owner_cookie_ = TF_INVALID_COOKIE;
            }
            if (context_ && edit_sink_cookie_ != TF_INVALID_COOKIE)
            {
                if (auto source = context_.try_as<ITfSource>(); source)
                {
                    (void)source->UnadviseSink(edit_sink_cookie_);
                }
                edit_sink_cookie_ = TF_INVALID_COOKIE;
            }
            if (thread_mgr_)
            {
                (void)thread_mgr_->Deactivate();
            }
            context_ = nullptr;
            document_mgr_ = nullptr;
            thread_ex_ = nullptr;
            thread_mgr_ = nullptr;
            if (com_initialized_)
            {
                ::CoUninitialize();
                com_initialized_ = false;
            }
        }

        void focus() noexcept
        {
            if (thread_mgr_ && document_mgr_)
            {
                (void)thread_mgr_->SetFocus(document_mgr_.get());
            }
        }

        void unfocus() noexcept
        {
            if (thread_mgr_)
            {
                (void)thread_mgr_->SetFocus(nullptr);
            }
        }

        // ITfContextOwner
        STDMETHODIMP GetACPFromPoint(const POINT *, DWORD, LONG *) noexcept override
        {
            return E_NOTIMPL;
        }

        STDMETHODIMP GetTextExt(LONG, LONG, RECT *rect, BOOL *clipped) noexcept override
        {
            if (rect)
            {
                *rect = caret_ ? caret_() : RECT{};
                const auto shift = (rect->right - rect->left) * 2;
                rect->left -= shift;
                rect->right -= shift;
                LOG2("tsf caret rect=%ld,%ld,%ld,%ld shift=%ld", rect->left, rect->top, rect->right, rect->bottom,
                     shift);
            }
            if (clipped)
            {
                *clipped = FALSE;
            }
            return S_OK;
        }

        STDMETHODIMP GetScreenExt(RECT *rect) noexcept override
        {
            if (rect)
            {
                *rect = screen_rect();
            }
            return S_OK;
        }

        STDMETHODIMP GetStatus(TF_STATUS *status) noexcept override
        {
            if (status)
            {
                status->dwDynamicFlags = 0;
                status->dwStaticFlags = TS_SS_TRANSITORY | TS_SS_NOHIDDENTEXT;
            }
            return S_OK;
        }

        STDMETHODIMP GetWnd(HWND *window) noexcept override
        {
            if (window)
            {
                *window = window_;
            }
            return S_OK;
        }

        STDMETHODIMP GetAttribute(REFGUID, VARIANT *value) noexcept override
        {
            if (value)
            {
                value->vt = VT_EMPTY;
            }
            return S_OK;
        }

        // ITfContextOwnerCompositionSink
        STDMETHODIMP OnStartComposition(ITfCompositionView *, BOOL *ok) noexcept override
        {
            LOG2("tsf composition start");
            if (ok)
            {
                *ok = TRUE;
            }
            return S_OK;
        }

        STDMETHODIMP OnUpdateComposition(ITfCompositionView *, ITfRange *) noexcept override
        {
            request_update();
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView *) noexcept override
        {
            LOG2("tsf composition end");
            request_update();
            return S_OK;
        }

        // ITfTextEditSink
        STDMETHODIMP OnEndEdit(ITfContext *, TfEditCookie, ITfEditRecord *) noexcept override
        {
            LOG2("tsf end edit");
            request_update();
            return S_OK;
        }

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID iid, void **object) noexcept override
        {
            if (object == nullptr)
            {
                return E_POINTER;
            }
            if (iid == IID_IUnknown || iid == IID_ITfContextOwner)
            {
                *object = static_cast<ITfContextOwner *>(this);
                AddRef();
                return S_OK;
            }
            if (iid == IID_ITfContextOwnerCompositionSink)
            {
                *object = static_cast<ITfContextOwnerCompositionSink *>(this);
                AddRef();
                return S_OK;
            }
            if (iid == IID_ITfTextEditSink)
            {
                *object = static_cast<ITfTextEditSink *>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() noexcept override
        {
            return ++references_;
        }

        STDMETHODIMP_(ULONG) Release() noexcept override
        {
            return --references_;
        }

      private:
        struct read_session final : ITfEditSession
        {
            explicit read_session(tsf_host &host) noexcept : host_{host}
            {
            }

            STDMETHODIMP QueryInterface(REFIID iid, void **object) noexcept override
            {
                if (object == nullptr)
                {
                    return E_POINTER;
                }
                if (iid == IID_IUnknown || iid == IID_ITfEditSession)
                {
                    *object = static_cast<ITfEditSession *>(this);
                    AddRef();
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }

            STDMETHODIMP_(ULONG) AddRef() noexcept override
            {
                return ++references_;
            }

            STDMETHODIMP_(ULONG) Release() noexcept override
            {
                return --references_;
            }

            STDMETHODIMP DoEditSession(TfEditCookie cookie) noexcept override
            {
                host_.read_context(cookie);
                return S_OK;
            }

            tsf_host &host_;
            ULONG references_{1};
        };

        void request_update() noexcept
        {
            if (!context_)
            {
                return;
            }
            HRESULT session_result = S_OK;
            const auto hr = context_->RequestEditSession(client_id_, &edit_session_,
                                                         TF_ES_ASYNC | TF_ES_READWRITE, &session_result);
            if (FAILED(hr) || FAILED(session_result))
            {
                LOG2("tsf edit session failed hr=%08x result=%08x", static_cast<unsigned>(hr),
                     static_cast<unsigned>(session_result));
            }
        }

        [[nodiscard]] static std::wstring range_text(TfEditCookie cookie, ITfRange *range)
        {
            std::wstring result;
            wchar_t buffer[128]{};
            for (;;)
            {
                ULONG length = 0;
                const auto hr = range->GetText(cookie, TF_TF_MOVESTART, buffer,
                                               static_cast<ULONG>(std::size(buffer)), &length);
                if (FAILED(hr) || length == 0)
                {
                    break;
                }
                result.append(buffer, length);
                if (length < std::size(buffer))
                {
                    break;
                }
            }
            return result;
        }

        [[nodiscard]] static bool range_is_composing(TfEditCookie cookie, ITfReadOnlyProperty *properties,
                                                     ITfRange *range)
        {
            VARIANT value;
            VariantInit(&value);
            if (FAILED(properties->GetValue(cookie, range, &value)))
            {
                return false;
            }

            bool composing = false;
            if (V_VT(&value) == VT_UNKNOWN && V_UNKNOWN(&value) != nullptr)
            {
                winrt::com_ptr<IEnumTfPropertyValue> values;
                if (SUCCEEDED(V_UNKNOWN(&value)->QueryInterface(IID_PPV_ARGS(values.put()))))
                {
                    for (;;)
                    {
                        TF_PROPERTYVAL item{};
                        ULONG fetched = 0;
                        if (values->Next(1, &item, &fetched) != S_OK || fetched == 0)
                        {
                            break;
                        }
                        if (IsEqualGUID(item.guidId, GUID_PROP_COMPOSING) && item.varValue.vt == VT_I4)
                        {
                            composing = item.varValue.lVal != 0;
                        }
                        VariantClear(&item.varValue);
                    }
                }
            }
            else if (V_VT(&value) == VT_I4)
            {
                composing = V_I4(&value) != 0;
            }
            VariantClear(&value);
            return composing;
        }

        void read_context(TfEditCookie cookie) noexcept
        {
            if (!context_)
            {
                return;
            }

            winrt::com_ptr<ITfRange> full;
            if (FAILED(context_->GetStart(cookie, full.put())))
            {
                return;
            }
            LONG full_length = 0;
            if (FAILED(full->ShiftEnd(cookie, LONG_MAX, &full_length, nullptr)))
            {
                return;
            }

            const GUID *properties_list[]{&GUID_PROP_COMPOSING};
            winrt::com_ptr<ITfReadOnlyProperty> properties;
            if (FAILED(context_->TrackProperties(properties_list, 1, nullptr, 0, properties.put())))
            {
                return;
            }

            winrt::com_ptr<IEnumTfRanges> ranges;
            if (FAILED(properties->EnumRanges(cookie, ranges.put(), full.get())))
            {
                return;
            }

            std::wstring finalized;
            std::wstring composing;
            for (;;)
            {
                ITfRange *items[8]{};
                ULONG count = 0;
                const auto next = ranges->Next(8, items, &count);
                for (ULONG i = 0; i < count; ++i)
                {
                    auto *range = items[i];
                    auto text = range_text(cookie, range);
                    if (range_is_composing(cookie, properties.get(), range))
                    {
                        composing.append(text);
                    }
                    else
                    {
                        finalized.append(text);
                    }
                    range->Release();
                }
                if (next != S_OK)
                {
                    break;
                }
            }

            LOG2("tsf context finalized=%zu composing=%zu", finalized.size(), composing.size());

            if (!finalized.empty())
            {
                std::wstring_view pending = finalized;
                if (finalized.size() > committed_text_.size() &&
                    finalized.compare(0, committed_text_.size(), committed_text_) == 0)
                {
                    pending = std::wstring_view{finalized}.substr(committed_text_.size());
                }
                if (!pending.empty() && commit_)
                {
                    commit_(pending);
                }
                committed_text_ = finalized;
            }
            else
            {
                committed_text_.clear();
            }

            if (composition_)
            {
                composition_(composing);
            }
        }

        [[nodiscard]] RECT screen_rect() const noexcept
        {
            RECT client{};
            if (window_ && ::GetClientRect(window_, &client))
            {
                POINT origin{0, 0};
                ::ClientToScreen(window_, &origin);
                client.left += origin.x;
                client.right += origin.x;
                client.top += origin.y;
                client.bottom += origin.y;
            }
            return client;
        }

        HWND window_{};
        text_sink commit_;
        text_sink composition_;
        rect_provider caret_;
        winrt::com_ptr<ITfThreadMgr> thread_mgr_;
        winrt::com_ptr<ITfThreadMgrEx> thread_ex_;
        winrt::com_ptr<ITfDocumentMgr> document_mgr_;
        winrt::com_ptr<ITfContext> context_;
        TfClientId client_id_{TF_CLIENTID_NULL};
        TfEditCookie edit_cookie_{TF_INVALID_EDIT_COOKIE};
        DWORD owner_cookie_{TF_INVALID_COOKIE};
        DWORD edit_sink_cookie_{TF_INVALID_COOKIE};
        read_session edit_session_;
        std::wstring committed_text_;
        ULONG references_{1};
        bool com_initialized_{};
    };
}
