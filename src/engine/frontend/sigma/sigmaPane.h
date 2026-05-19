/////////////////////////////////////////////////////////////////////////////
// ibSigmaPane — Sigma AI chat / agent surface inside OES Designer.
//
// A wxPanel that wraps wxWebView. The WebView loads an HTML/JS/CSS
// bundle supplied by an AI provider plugin (e.g. pugi-oes-bridge). All
// UX (transcript, prompt input, mode switch, code-block rendering)
// lives inside the WebView; the host owns wiring, persistence, and the
// dockable container only.
//
// Communication with the plugin is JSON over the wxWebView script
// message handler — `wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED` carries
// `window.sigma.postMessage(jsonString)` calls from the JS side, and
// the host pushes back via `wxWebView::RunScript("window.sigma._recv(...)")`.
//
// See docs/specs/sigma-ai-chat-pane-2026-05-20.md §"Architecture" for
// the full host ↔ plugin contract.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_SIGMA_PANE_H_
#define _IB_SIGMA_PANE_H_

#include "frontend/mainFrame/mainFrame.h"  // FRONTEND_API

#include "backend/plugin/pluginApi.h"      // ibPluginWebMsgFn

#include <wx/panel.h>
#include <wx/string.h>

#include <string>

class wxWebView;
class wxWebViewEvent;

class FRONTEND_API ibSigmaPane : public wxPanel {
public:
	// paneId is the stable identifier the plugin registered (e.g.
	// "pugi.sigma.chat"). title is the visible caption (e.g. "Sigma").
	// htmlBundlePath is an absolute filesystem path to the entry HTML
	// the WebView loads via file:// URL — typically <plugin>/webview/index.html.
	// onMessage is the C callback the plugin gave us for JS-originated
	// messages. userData is the plugin's opaque handle, forwarded back
	// on every invocation.
	ibSigmaPane(wxWindow* parent,
	             const wxString& paneId,
	             const wxString& title,
	             const wxString& htmlBundlePath,
	             ibPluginWebMsgFn onMessage,
	             void* userData);

	// Push a JSON message from the host into the WebView. Thread-safe;
	// when called off the UI thread, marshals through wxQueueEvent
	// before invoking wxWebView::RunScript.
	void PushMessage(const wxString& jsonInline);

	const wxString& GetPaneId() const { return m_paneId; }
	const wxString& GetTitle()  const { return m_title;  }

private:
	wxWebView*       m_webView    = nullptr;
	wxString         m_paneId;
	wxString         m_title;
	ibPluginWebMsgFn m_onMessage  = nullptr;
	void*            m_userData   = nullptr;

	void OnScriptMessage(wxWebViewEvent& event);
	void OnPushFromOtherThread(wxThreadEvent& event);
};

#endif // _IB_SIGMA_PANE_H_
