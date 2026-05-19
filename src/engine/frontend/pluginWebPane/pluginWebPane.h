/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane — generic plugin-driven WebView pane inside the host.
//
// Platform infrastructure for embedding plugin-supplied web UI (HTML/JS/CSS)
// in a dockable Designer pane. Used by AI assistant plugins, future custom
// integration UIs, anything that ships its own browser surface.
//
// A wxPanel wrapping wxWebView. The WebView loads an HTML bundle the plugin
// supplies. Host owns the dockable container, lifecycle, persistence, and
// the bidirectional JSON channel; UX inside the WebView is plugin code.
//
// Channel contract:
//   - JS → host:  window.oesHost.postMessage(jsonString)
//                 → wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED →
//                 plugin's ibPluginWebMsgFn callback (paneId, jsonInline, userData)
//   - host → JS:  PushMessage(jsonInline) → wxWebView::RunScript("window.oesHost._recv(...)")
//                 Thread-safe; off-UI callers marshal via wxQueueEvent.
//
// Plugins drive panes through the ABI v4 host callbacks:
//   ibHostAPI::RegisterWebPane, WebPaneSend, WebPaneShow.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_WEB_PANE_H_
#define _IB_PLUGIN_WEB_PANE_H_

#include "frontend/mainFrame/mainFrame.h"  // FRONTEND_API

#include "backend/plugin/pluginApi.h"      // ibPluginWebMsgFn

#include <wx/panel.h>
#include <wx/string.h>

class wxWebView;
class wxWebViewEvent;

class FRONTEND_API ibPluginWebPane : public wxPanel {
public:
	// paneId is the stable identifier the plugin registered. title is the
	// visible AUI caption. htmlBundlePath is an absolute filesystem path
	// to the entry HTML loaded via file:// URL. onMessage is the C
	// callback for JS-originated messages. userData is the plugin's
	// opaque handle, forwarded on every invocation.
	ibPluginWebPane(wxWindow* parent,
	                const wxString& paneId,
	                const wxString& title,
	                const wxString& htmlBundlePath,
	                ibPluginWebMsgFn onMessage,
	                void* userData);

	// Push a JSON message from the host into the WebView. Thread-safe;
	// off-UI callers marshal through wxQueueEvent before RunScript.
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

#endif // _IB_PLUGIN_WEB_PANE_H_
