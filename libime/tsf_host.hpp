#pragma once

// Minimal TSF client for the windowed console. The TSF transparency context
// owns the composition ranges; committed composition text is forwarded to the
// console input queue by the provider callback.

#include <Windows.h>
#include <msctf.h>
#include <winrt/base.h>
#include "utility/log.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace corehost::window
{
    class tsf_host final : public ITfContextOwner, public ITfContextOwnerCompositionSink, public ITfTextEditSink
    {
      public:
        using text_sink = std::function<void(std::wstring_view)>;
        using rect_provider = std::function<RECT()>;

        tsf_host(HWND window, text_sink commit, text_sink composition, rect_provider caret)
            : window_{window}, commit_{std::move(commit)}, composition_{std::move(composition)},
              caret_{std::move(caret)}
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
                hr = thread_ex_->ActivateEx(&client_id_, TF_TMAE_NOACTIVATETIP | TF_TMAE_CONSOLE);
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
            if (auto source = context_.try_as<ITfSource>(); source)
            {
                (void)source->AdviseSink(IID_ITfContextOwner, static_cast<ITfContextOwner *>(this), &owner_cookie_);
                (void)source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this), &edit_sink_cookie_);
            }

            LOG2("tsf initialized client=%u context=%p", static_cast<unsigned>(client_id_), context_.get());
            pump_ = nullptr;
            LOG2("tsf pump disabled");
            (void)thread_mgr_->SetFocus(document_mgr_.get());
            return S_OK;
        }

        void shutdown() noexcept
        {
            if (thread_mgr_)
            {
                if (pump_)
                {
                    pump_ = nullptr;
                }
                (void)thread_mgr_->SetFocus(nullptr);
            }
            release_range();
            if (document_mgr_ && context_)
            {
                (void)document_mgr_->Pop(TF_POPF_ALL);
            }
            if (context_ && owner_cookie_ != TF_INVALID_COOKIE)
            {
                if (thread_ex_)
            {
                winrt::com_ptr<ITfDocumentMgr> previous;
                const auto associate = thread_ex_->AssociateFocus(window_, document_mgr_.get(), previous.put());
                LOG2("tsf associate focus hr=%08x", static_cast<unsigned>(associate));
            }
            if (auto source = context_.try_as<ITfSource>(); source)
                {
                    (void)source->UnadviseSink(owner_cookie_);
                }
                owner_cookie_ = TF_INVALID_COOKIE;
            }
            if (thread_mgr_)
            {
                (void)thread_mgr_->Deactivate();
            }
            context_ = nullptr;
            document_mgr_ = nullptr;
            pump_ = nullptr;
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

        [[nodiscard]] bool pump_message(MSG &message, bool &eaten) noexcept
        {
            if (!pump_)
            {
                return false;
            }
            BOOL was_eaten = FALSE;
            const auto hr = pump_->GetMessageW(&message, window_, 0, 0, &was_eaten);
            if (FAILED(hr))
            {
                return false;
            }
            eaten = was_eaten != FALSE;
            if (message.message == WM_KEYDOWN || message.message == WM_CHAR || message.message == WM_IME_COMPOSITION ||
                message.message == WM_IME_STARTCOMPOSITION || message.message == WM_IME_ENDCOMPOSITION)
            {
                LOG2("tsf pump msg=%04x eaten=%d", message.message, eaten ? 1 : 0);
            }
            return true;
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

        // ITfTextEditSink
        STDMETHODIMP OnEndEdit(ITfContext *, TfEditCookie, ITfEditRecord *) noexcept override
        {
            return S_OK;
        }

        // ITfContextOwnerCompositionSink
        STDMETHODIMP OnStartComposition(ITfCompositionView *view, BOOL *ok) noexcept override
        {
            LOG2("tsf composition start");
            if (ok)
            {
                *ok = TRUE;
            }
            retain_range(view, nullptr);
            return S_OK;
        }

        STDMETHODIMP OnUpdateComposition(ITfCompositionView *view, ITfRange *range) noexcept override
        {
            retain_range(view, range);
            request_read(false);
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView *view) noexcept override
        {
            LOG2("tsf composition end");
            retain_range(view, nullptr);
            request_read(true);
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
            const auto value = --references_;
            if (value == 0)
            {
                // Owned by window_host; do not delete through COM.
            }
            return value;
        }

      private:
        struct read_session final : ITfEditSession
        {
            explicit read_session(tsf_host &host, bool commit) noexcept : host_{host}, commit_{commit}
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
                const auto value = --references_;
                if (value == 0)
                {
                    delete this;
                }
                return value;
            }

            STDMETHODIMP DoEditSession(TfEditCookie cookie) noexcept override
            {
                host_.read_composition(cookie, commit_);
                return S_OK;
            }

            tsf_host &host_;
            bool commit_{};
            ULONG references_{1};
        };

        void retain_range(ITfCompositionView *view, ITfRange *range) noexcept
        {
            release_range();
            if (range)
            {
                range_ = range;
                range_->AddRef();
                return;
            }
            if (view && SUCCEEDED(view->GetRange(&range_)))
            {
                // GetRange already returns a referenced range.
            }
        }

        void release_range() noexcept
        {
            if (range_)
            {
                range_->Release();
                range_ = nullptr;
            }
        }

        void request_read(bool commit) noexcept
        {
            if (!context_ || !range_)
            {
                return;
            }
            auto session = new (std::nothrow) read_session{*this, commit};
            if (session == nullptr)
            {
                return;
            }
            HRESULT session_result = S_OK;
            const auto hr = context_->RequestEditSession(client_id_, session, TF_ES_SYNC | TF_ES_READ,
                                                         &session_result);
            session->Release();
            if (FAILED(hr) || FAILED(session_result))
            {
                if (commit)
                {
                    release_range();
                }
            }
        }

        void read_composition(TfEditCookie cookie, bool commit) noexcept
        {
            if (!range_)
            {
                return;
            }

            std::wstring text;
            wchar_t buffer[128]{};
            for (;;)
            {
                ULONG length = 0;
                const auto hr = range_->GetText(cookie, TF_TF_MOVESTART, buffer,
                                                static_cast<ULONG>(std::size(buffer)), &length);
                if (FAILED(hr) || length == 0)
                {
                    break;
                }
                text.append(buffer, length);
                if (length < std::size(buffer))
                {
                    break;
                }
            }

            LOG2("tsf composition text length=%zu commit=%d", text.size(), commit ? 1 : 0);
            if (commit)
            {
                if (!text.empty() && commit_)
                {
                    commit_(text);
                }
                release_range();
            }
            else if (composition_)
            {
                composition_(text);
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
        winrt::com_ptr<ITfMessagePump> pump_;
        ITfRange *range_{};
        TfClientId client_id_{TF_CLIENTID_NULL};
        TfEditCookie edit_cookie_{TF_INVALID_EDIT_COOKIE};
        DWORD owner_cookie_{TF_INVALID_COOKIE};
        DWORD edit_sink_cookie_{TF_INVALID_COOKIE};
        ULONG references_{1};
        bool com_initialized_{};
    };
}
