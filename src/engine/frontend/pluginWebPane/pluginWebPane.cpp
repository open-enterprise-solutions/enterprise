/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane implementation. See header for the host ↔ plugin contract.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/pluginWebPane.h"

#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/uri.h>
#include <wx/filename.h>
#include <wx/log.h>

namespace {

// Event ID for our cross-thread push-to-WebView channel.
wxDEFINE_EVENT(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD, wxThreadEvent);

// Lightweight JS-string escaper. Wraps a payload in quotes so it can be
// inlined inside RunScript without pulling in a JSON library — the payload
// is already a JSON document; only wrapping quotes, control chars, and the
// two JS-illegal line separators (U+2028, U+2029) need escaping.
wxString WrapAsJsString(const wxString& raw)
{
	wxString out;
	out.reserve(raw.size() * 2 + 4);
	out += wxT('"');
	for (auto it = raw.begin(); it != raw.end(); ++it) {
		const wxUniChar c = *it;
		const auto v = static_cast<unsigned>(c.GetValue());
		if      (c == wxT('\\')) out += wxT("\\\\");
		else if (c == wxT('"'))  out += wxT("\\\"");
		else if (c == wxT('\n')) out += wxT("\\n");
		else if (c == wxT('\r')) out += wxT("\\r");
		else if (c == wxT('\t')) out += wxT("\\t");
		else if (v == 0x2028)    out += wxT("\\u2028"); // JS-illegal in string literals (ES <2019)
		else if (v == 0x2029)    out += wxT("\\u2029");
		else if (v < 0x20) {
			out += wxString::Format(wxT("\\u%04x"), v);
		}
		else                     out += c;
	}
	out += wxT('"');
	return out;
}

// Boot script — registers the bridge BEFORE any page-author code runs.
// Installed via wxWebView::AddUserScript with TimeStart = document-start
// (or via the available wxWebView option flag) so DOMContentLoaded handlers
// always see window.oesHost ready.
//
// Channel contract:
//   - window.oesHost.postMessage(jsonString) routes JS → host
//   - window.oesHost._recv(jsonString) is the host's push entry point.
//     Default impl drains into a private __inbox queue; the page replaces
//     _recv on DOMContentLoaded and drains its leftovers.
//   - No global window.dispatchEvent / MessageEvent — third-party scripts
//     in the bundle cannot eavesdrop on host pushes by listening for
//     'message' events on window.
wxString PluginWebPaneBootScript()
{
	// Use raw narrow string + FromUTF8 to avoid MSVC narrow/wide concat
	// pitfalls when wxT() widens at compile time.
	const char* js =
		"(function(){\n"
		"  if (window.oesHost && window.oesHost.__ready) return;\n"
		"  var inbox = [];\n"
		"  var host = window.oesHost = window.oesHost || {};\n"
		"  host.__inbox = inbox;\n"
		"  host.postMessage = function(s){\n"
		"    try { if (window.wx && typeof window.wx.postMessage === 'function') window.wx.postMessage(s); }\n"
		"    catch (e) { console.error('oesHost.postMessage', e); }\n"
		"  };\n"
		"  host._recv = function(s){ try { inbox.push(s); } catch (e) { console.error('oesHost._recv default', e); } };\n"
		"  host.__ready = true;\n"
		"})();\n";
	return wxString::FromUTF8(js);
}

} // namespace

ibPluginWebPane::ibPluginWebPane(wxWindow* parent,
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
	m_webView = wxWebView::New(this, wxID_ANY);
	if (m_webView == nullptr) {
		wxLogWarning(wxT("ibPluginWebPane: wxWebView::New returned null on this platform"));
		return;
	}

	// Script-message channel. wxWebView versions differ on whether
	// AddScriptMessageHandler returns bool — older 3.2.x returns void on
	// some backends. Wrap in a try/log so a backend that can't register a
	// handler degrades gracefully instead of silently breaking the bridge.
	if (!m_webView->AddScriptMessageHandler(wxT("wx"))) {
		wxLogWarning(wxT("ibPluginWebPane[%s]: AddScriptMessageHandler returned false — inbound channel may be unavailable"),
		             paneId);
	}

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_webView, 1, wxEXPAND);
	SetSizer(sizer);

	m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
	                  &ibPluginWebPane::OnScriptMessage, this);

	Bind(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD,
	     &ibPluginWebPane::OnPushFromOtherThread, this);

	// Inject the boot script at document-start. AddUserScript runs the
	// snippet BEFORE the page's own scripts evaluate, so DOMContentLoaded
	// handlers and inline <script> tags both see window.oesHost ready.
	// This replaces the prior wxEVT_WEBVIEW_LOADED wiring which fired
	// AFTER DOMContentLoaded and missed the early bind window.
	m_webView->AddUserScript(PluginWebPaneBootScript(), wxWEBVIEW_INJECT_AT_DOCUMENT_START);

	const wxString fileUri = wxFileName::FileNameToURL(wxFileName(htmlBundlePath));
	m_webView->LoadURL(fileUri);
}

void ibPluginWebPane::PushMessage(const wxString& jsonInline)
{
	if (m_webView == nullptr) return;

	if (wxThread::IsMain()) {
		const wxString js = wxT("window.oesHost && window.oesHost._recv && window.oesHost._recv(")
		                     + WrapAsJsString(jsonInline)
		                     + wxT(");");
		m_webView->RunScript(js);
		return;
	}

	// Off-thread: marshal through wxThreadEvent. wxWebView is not
	// thread-safe — direct access from a worker crashes silently on Windows.
	auto* evt = new wxThreadEvent(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD);
	evt->SetString(jsonInline);
	wxQueueEvent(this, evt);
}

void ibPluginWebPane::OnPushFromOtherThread(wxThreadEvent& event)
{
	const wxString payload = event.GetString();
	const wxString js = wxT("window.oesHost && window.oesHost._recv && window.oesHost._recv(")
	                     + WrapAsJsString(payload)
	                     + wxT(");");
	if (m_webView) m_webView->RunScript(js);
}

void ibPluginWebPane::OnScriptMessage(wxWebViewEvent& event)
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
