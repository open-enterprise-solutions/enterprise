/////////////////////////////////////////////////////////////////////////////
// ibSigmaPane implementation. See header for the host ↔ plugin contract.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/sigma/sigmaPane.h"

#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/uri.h>
#include <wx/filename.h>
#include <wx/log.h>

namespace {

// Event ID for our cross-thread push-to-WebView channel.
wxDEFINE_EVENT(IB_SIGMA_PUSH_FROM_THREAD, wxThreadEvent);

// Lightweight JS-string escaper. Wraps a payload in quotes so it can
// be inlined inside a `RunScript` call without pulling in a JSON
// library — the payload is already a JSON document; only the wrapping
// quote characters and a handful of control chars need escaping.
wxString WrapAsJsString(const wxString& raw)
{
	wxString out;
	out.reserve(raw.size() + 4);
	out += wxT('"');
	for (auto it = raw.begin(); it != raw.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('\\')) out += wxT("\\\\");
		else if (c == wxT('"'))  out += wxT("\\\"");
		else if (c == wxT('\n')) out += wxT("\\n");
		else if (c == wxT('\r')) out += wxT("\\r");
		else if (c == wxT('\t')) out += wxT("\\t");
		else if (c.GetValue() < 0x20) {
			out += wxString::Format(wxT("\\u%04x"),
			                          static_cast<unsigned>(c.GetValue()));
		}
		else                     out += c;
	}
	out += wxT('"');
	return out;
}

// Inject a tiny boot script before any page-author code runs so the
// host knows the message channel exists immediately on load. Mirrors
// the contract VS Code WebView API publishes — `window.<bridge>` with
// postMessage + addEventListener for inbound messages.
wxString SigmaBootScript()
{
	return wxT(R"JS(
		(function() {
			if (window.sigma && window.sigma.__ready) return;
			window.sigma = window.sigma || {};
			window.sigma.__inbox = [];
			window.sigma.postMessage = function(s) {
				try {
					if (window.wx && typeof window.wx.postMessage === 'function') {
						window.wx.postMessage(s);
					}
				} catch (e) { console.error('sigma.postMessage', e); }
			};
			window.sigma._recv = function(s) {
				try {
					var evt = new MessageEvent('message', { data: s });
					window.sigma.__inbox.push(s);
					window.dispatchEvent(evt);
				} catch (e) { console.error('sigma._recv', e); }
			};
			window.sigma.__ready = true;
		})();
	)JS");
}

} // namespace

ibSigmaPane::ibSigmaPane(wxWindow* parent,
                          const wxString& paneId,
                          const wxString& title,
                          const wxString& htmlBundlePath,
                          ibPluginWebMsgFn onMessage,
                          void* userData)
    : wxPanel(parent, wxID_ANY)
    , m_paneId(paneId)
    , m_title(title)
    , m_onMessage(onMessage)
    , m_userData(userData)
{
	// Backend selection: prefer the OS-native engine. wxWebView::New
	// returns nullptr if the platform has no usable backend (rare —
	// macOS has WebKit, Windows has Edge/Chromium, Linux ships WebKit2GTK).
	m_webView = wxWebView::New(this, wxID_ANY);
	if (m_webView == nullptr) {
		// Defer the failure to the host trampoline — degrade by showing
		// nothing. RegisterWebPane returns non-zero so the plugin sees
		// the error.
		wxLogWarning(wxT("ibSigmaPane: wxWebView::New returned null on this platform"));
		return;
	}

	// Script-message channel. The page calls window.wx.postMessage()
	// (inside the boot script we name it window.sigma.postMessage); the
	// platform routes that through to wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED.
	m_webView->AddScriptMessageHandler(wxT("wx"));

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_webView, 1, wxEXPAND);
	SetSizer(sizer);

	m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
	                  &ibSigmaPane::OnScriptMessage, this);

	// Cross-thread push entry point — PushMessage(jsonInline) from any
	// thread queues a wxThreadEvent that fires on the UI loop and
	// dispatches to wxWebView::RunScript on the right thread.
	Bind(IB_SIGMA_PUSH_FROM_THREAD,
	     &ibSigmaPane::OnPushFromOtherThread, this);

	// Inject the host boot script every time a fresh document loads so
	// the channel survives reload / navigation.
	m_webView->Bind(wxEVT_WEBVIEW_LOADED,
	    [this](wxWebViewEvent&) {
	        if (m_webView) m_webView->RunScript(SigmaBootScript());
	    });

	// Bootstrap the page itself. The bundle path is filesystem-absolute;
	// wxWebView accepts a file:// URL on every platform.
	const wxString fileUri = wxFileName::FileNameToURL(wxFileName(htmlBundlePath));
	m_webView->LoadURL(fileUri);
}

void ibSigmaPane::PushMessage(const wxString& jsonInline)
{
	if (m_webView == nullptr) return;

	if (wxThread::IsMain()) {
		// Direct path — already on UI thread; call RunScript inline.
		const wxString js = wxT("window.sigma && window.sigma._recv && window.sigma._recv(")
		                     + WrapAsJsString(jsonInline)
		                     + wxT(");");
		m_webView->RunScript(js);
		return;
	}

	// Off-thread: marshal through the wxThreadEvent so wxWebView is
	// only ever touched from the UI thread. WebViews are not
	// thread-safe — direct access from a worker crashes silently on
	// Windows.
	auto* evt = new wxThreadEvent(IB_SIGMA_PUSH_FROM_THREAD);
	evt->SetString(jsonInline);
	wxQueueEvent(this, evt);
}

void ibSigmaPane::OnPushFromOtherThread(wxThreadEvent& event)
{
	const wxString payload = event.GetString();
	const wxString js = wxT("window.sigma && window.sigma._recv && window.sigma._recv(")
	                     + WrapAsJsString(payload)
	                     + wxT(");");
	if (m_webView) m_webView->RunScript(js);
}

void ibSigmaPane::OnScriptMessage(wxWebViewEvent& event)
{
	if (m_onMessage == nullptr) return;
	const wxScopedCharBuffer utf8 = event.GetString().utf8_str();
	const wxScopedCharBuffer paneIdUtf8 = m_paneId.utf8_str();
	try {
		m_onMessage(paneIdUtf8.data(), utf8.data(), m_userData);
	} catch (...) {
		// Plugin bug — swallow to keep the host alive.
	}
}

