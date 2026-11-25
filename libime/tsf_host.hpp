#pragma once

// Minimal TSF client for the windowed console. COM apartment lifetime belongs
// to the caller; this class only owns TSF objects and its edit-session buffers.

#include <Windows.h>
#include <cassert>
#include <msctf.h>
#include <winrt/base.h>

#include <string>
#include <string_view>

namespace corehost::window
{
    class tsf_host final : public ITfContextOwner, public ITfContextOwnerCompositionSink, public ITfTextEditSink
    {
      public:
        using text_callback = void (*)(void *context, std::wstring_view text) noexcept;
        using caret_callback = RECT (*)(void *context) noexcept;

        struct callbacks final
        {
            void *context{};
            text_callback commit{};
            text_callback composition{};
            caret_callback caret{};
        };

        tsf_host(HWND window, callbacks callbacks) try : window_{window}, callbacks_{callbacks},
                                                         edit_session_{*this}
        {
            winrt::check_hresult(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(thread_mgr_.put())));

            thread_ex_ = thread_mgr_.try_as<ITfThreadMgrEx>();
            if (thread_ex_)
            {
                winrt::check_hresult(thread_ex_->ActivateEx(
                    &client_id_, TF_TMAE_NOACTIVATETIP | TF_TMAE_NOACTIVATEKEYBOARDLAYOUT | TF_TMAE_CONSOLE));
            }
            else
            {
                winrt::check_hresult(thread_mgr_->Activate(&client_id_));
            }

            winrt::check_hresult(thread_mgr_->CreateDocumentMgr(document_mgr_.put()));
            winrt::check_hresult(document_mgr_->CreateContext(
                client_id_, 0, static_cast<ITfContextOwnerCompositionSink *>(this), context_.put(), &edit_cookie_));

            auto source = context_.try_as<ITfSource>();
            winrt::check_hresult(source ? S_OK : E_NOINTERFACE);
            winrt::check_hresult(source->AdviseSink(IID_ITfContextOwner, static_cast<ITfContextOwner *>(this),
                                                    &owner_cookie_));
            winrt::check_hresult(source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this),
                                                    &edit_sink_cookie_));

            winrt::check_hresult(document_mgr_->Push(context_.get()));

            if (thread_ex_)
            {
                winrt::com_ptr<ITfDocumentMgr> previous;
                winrt::check_hresult(thread_ex_->AssociateFocus(window_, document_mgr_.get(), previous.put()));
            }

            winrt::check_hresult(thread_mgr_->SetFocus(document_mgr_.get()));
            LOG2("tsf initialized client=%u context=%p", static_cast<unsigned>(client_id_), context_.get());
        }
        catch (const winrt::hresult_error &error)
        {
            LOG2("tsf initialize failed hr=%08x", static_cast<unsigned>(error.code()));
            window_ = nullptr;
            thread_mgr_ = nullptr;
            thread_ex_ = nullptr;
            document_mgr_ = nullptr;
            context_ = nullptr;
        }

        ~tsf_host() noexcept
        {
            if (window_ == nullptr)
            {
                return;
            }

            assert(thread_mgr_);
            assert(document_mgr_);
            assert(context_);
            (void)thread_mgr_->SetFocus(nullptr);

            auto source = context_.try_as<ITfSource>();
            assert(source);
            assert(owner_cookie_ != TF_INVALID_COOKIE);
            assert(edit_sink_cookie_ != TF_INVALID_COOKIE);
            (void)source->UnadviseSink(owner_cookie_);
            (void)source->UnadviseSink(edit_sink_cookie_);

            (void)document_mgr_->Pop(TF_POPF_ALL);
            (void)thread_mgr_->Deactivate();
        }
        tsf_host(const tsf_host &) = delete;
        tsf_host &operator=(const tsf_host &) = delete;

        void focus() noexcept
        {
            if (window_ == nullptr)
            {
                return;
            }
            assert(thread_mgr_);
            assert(document_mgr_);
            if (FAILED(thread_mgr_->SetFocus(document_mgr_.get())))
            {
                return;
            }
        }

        void unfocus() noexcept
        {
            if (window_ == nullptr)
            {
                return;
            }
            assert(thread_mgr_);
            if (FAILED(thread_mgr_->SetFocus(nullptr)))
            {
                return;
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
                assert(callbacks_.caret);
                *rect = callbacks_.caret(callbacks_.context);
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

        STDMETHODIMP OnUpdateComposition(ITfCompositionView *, ITfRange *) noexcept
        try
        {
            request_update();
            return S_OK;
        }
        catch (const winrt::hresult_error &error)
        {
            LOG2("tsf update composition failed hr=%08x", static_cast<unsigned>(error.code()));
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView *) noexcept
        try
        {
            LOG2("tsf composition end");
            request_update();
            return S_OK;
        }
        catch (const winrt::hresult_error &error)
        {
            LOG2("tsf end composition failed hr=%08x", static_cast<unsigned>(error.code()));
            return S_OK;
        }

        // ITfTextEditSink
        STDMETHODIMP OnEndEdit(ITfContext *, TfEditCookie, ITfEditRecord *) noexcept
        try
        {
            LOG2("tsf end edit");
            request_update();
            return S_OK;
        }
        catch (const winrt::hresult_error &error)
        {
            LOG2("tsf end edit failed hr=%08x", static_cast<unsigned>(error.code()));
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

            STDMETHODIMP DoEditSession(TfEditCookie cookie) noexcept
            try
            {
                host_.read_context(cookie);
                return S_OK;
            }
            catch (const winrt::hresult_error &error)
            {
                LOG2("tsf read session failed hr=%08x", static_cast<unsigned>(error.code()));
                return S_OK;
            }

            tsf_host &host_;
            ULONG references_{1};
        };

        void request_update()
        {
            assert(context_);
            HRESULT session_result = S_OK;
            winrt::check_hresult(context_->RequestEditSession(client_id_, &edit_session_,
                                                              TF_ES_ASYNC | TF_ES_READWRITE,
                                                              &session_result));
            winrt::check_hresult(session_result);
        }

        static void append_range_text(TfEditCookie cookie, ITfRange *range, std::wstring &target)
        {
            wchar_t buffer[256]{};
            for (;;)
            {
                ULONG length = 0;
                winrt::check_hresult(range->GetText(cookie, TF_TF_MOVESTART, buffer,
                                                    static_cast<ULONG>(std::size(buffer)), &length));
                if (length == 0)
                {
                    break;
                }
                target.append(buffer, length);
                if (length < std::size(buffer))
                {
                    break;
                }
            }
        }

        [[nodiscard]] static bool range_is_composing(TfEditCookie cookie, ITfReadOnlyProperty *properties,
                                                     ITfRange *range)
        {
            VARIANT value;
            VariantInit(&value);
            winrt::check_hresult(properties->GetValue(cookie, range, &value));

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
                        const auto next = values->Next(1, &item, &fetched);
                        if (FAILED(next) || fetched == 0)
                        {
                            break;
                        }
                        if (IsEqualGUID(item.guidId, GUID_PROP_COMPOSING) && item.varValue.vt == VT_I4)
                        {
                            composing = item.varValue.lVal != 0;
                        }
                        VariantClear(&item.varValue);
                        if (next != S_OK)
                        {
                            break;
                        }
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

        void read_context(TfEditCookie cookie)
        {
            assert(context_);

                winrt::com_ptr<ITfRange> full;
                winrt::check_hresult(context_->GetStart(cookie, full.put()));
                LONG full_length = 0;
                winrt::check_hresult(full->ShiftEnd(cookie, LONG_MAX, &full_length, nullptr));

                const GUID *properties_list[]{&GUID_PROP_COMPOSING};
                winrt::com_ptr<ITfReadOnlyProperty> properties;
                winrt::check_hresult(context_->TrackProperties(properties_list, 1, nullptr, 0, properties.put()));

                winrt::com_ptr<IEnumTfRanges> ranges;
                winrt::check_hresult(properties->EnumRanges(cookie, ranges.put(), full.get()));

                finalized_buffer_.clear();
                composing_buffer_.clear();
                for (;;)
                {
                    ITfRange *items[8]{};
                    ULONG count = 0;
                    const auto next = ranges->Next(8, items, &count);
                    if (FAILED(next))
                    {
                        winrt::check_hresult(next);
                    }
                    for (ULONG i = 0; i < count; ++i)
                    {
                        auto *range = items[i];
                        const auto is_composing = range_is_composing(cookie, properties.get(), range);
                        append_range_text(cookie, range, is_composing ? composing_buffer_ : finalized_buffer_);
                        range->Release();
                    }
                    if (next != S_OK)
                    {
                        break;
                    }
                }

                LOG2("tsf context finalized=%zu composing=%zu", finalized_buffer_.size(), composing_buffer_.size());

                if (!finalized_buffer_.empty())
                {
                    std::wstring_view pending = finalized_buffer_;
                    if (finalized_buffer_.size() > committed_text_.size() &&
                        finalized_buffer_.compare(0, committed_text_.size(), committed_text_) == 0)
                    {
                        pending = std::wstring_view{finalized_buffer_}.substr(committed_text_.size());
                    }
                    assert(callbacks_.commit);
                    if (!pending.empty())
                    {
                        callbacks_.commit(callbacks_.context, pending);
                    }
                    committed_text_ = finalized_buffer_;

                    winrt::com_ptr<ITfRange> start;
                    winrt::check_hresult(context_->GetStart(cookie, start.put()));
                    LONG shifted = 0;
                    winrt::check_hresult(
                        start->ShiftEnd(cookie, static_cast<LONG>(finalized_buffer_.size()), &shifted, nullptr));
                    winrt::check_hresult(start->SetText(cookie, 0, nullptr, 0));
                    committed_text_.clear();
                }
                else
                {
                    committed_text_.clear();
                }

                assert(callbacks_.composition);
                callbacks_.composition(callbacks_.context, composing_buffer_);
            }

        [[nodiscard]] RECT screen_rect() const noexcept
        {
            assert(window_);
            RECT client{};
            const auto got_client = ::GetClientRect(window_, &client);
            assert(got_client);
            POINT origin{0, 0};
            const auto got_origin = ::ClientToScreen(window_, &origin);
            assert(got_origin);
            client.left += origin.x;
            client.right += origin.x;
            client.top += origin.y;
            client.bottom += origin.y;
            return client;
        }

        HWND window_{};
        callbacks callbacks_{};
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
        std::wstring finalized_buffer_;
        std::wstring composing_buffer_;
        ULONG references_{1};
    };
}
