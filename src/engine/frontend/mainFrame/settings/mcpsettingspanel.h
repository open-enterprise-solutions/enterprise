#ifndef MCP_SETTINGS_PANEL_H
#define MCP_SETTINGS_PANEL_H

/////////////////////////////////////////////////////////////////////////////
// The MCP page of the settings dialog — where a person says whether their own
// assistant may reach this designer, and where.
//
// THE SETTINGS ARE THEIRS, not the installation's: they are saved per user, so
// two developers on one machine run two servers on two ports. This panel only
// edits the value; who saves it and when is the dialog's caller (the designer),
// and where it is kept is the server's (sys_settings, category Mcp).
//
// The endpoint field is READ-ONLY on purpose. It is not a setting — it is what
// the platform HANDS OUT: the address a person pastes into their tool's own
// configuration. Showing it beside the port is what makes "configured" mean
// something a person can act on.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"       // FRONTEND_API — every exported class here needs it declared

#include <wx/wx.h>

#include "backend/mcp/mcpServer.h"   // ibMcpSettings — the value this page edits

class FRONTEND_API ibPanelMcpSettings : public wxPanel
{

public:

	ibPanelMcpSettings(wxWindow* parent, int id = wxID_ANY,
		wxPoint pos = wxDefaultPosition, wxSize size = wxSize(461, 438), int style = wxTAB_TRAVERSAL);

	void Initialize();

	void                 SetSettings(const ibMcpSettings& settings);
	const ibMcpSettings& GetSettings() const;

	// What the running server answers on, or empty when it is not running. The
	// panel does not invent it — the server is the one that knows whether it
	// actually took that port.
	void SetEndpoint(const wxString& endpoint);

	void OnEnabled(wxCommandEvent& event);
	void OnAddressChanged(wxCommandEvent& event);
	void OnPortChanged(wxCommandEvent& event);

	// Taking the handout away. Two, because there are two shapes of it and a single button would
	// have to guess which one a person meant.
	void OnCopyCommand(wxCommandEvent& event);
	void OnCopyBlock(wxCommandEvent& event);

	// ⭐ NOW, as opposed to the checkbox above, which is NEXT TIME. Starting and stopping used to
	// be a menu item that answered in a message box; it lives here because this is where the
	// address, the key and the ready-made command already are, and because a box that has been
	// dismissed cannot be read again.
	void OnRun(wxCommandEvent& event);

	wxDECLARE_EVENT_TABLE();

private:

	// Greyed out when the server is switched off — a port nobody listens on is
	// not a question worth asking.
	void UpdateEnabledState();

	// 🛑 THE SECRET HAD NO READER. The server MINTS a token when it is configured without one and
	// refuses every request that does not carry it — but nothing ever showed it, in any window or
	// any log. So the address alone was not a handout at all: a person following it got refused,
	// and no client could ever connect, which is why nothing ever woke one.
	//
	// Composed rather than shown field by field, because what a person needs is not four values —
	// it is the block their tool's configuration takes, ready to paste.
	void RefreshHandout();

	// One place that puts a field on the clipboard, so both buttons cannot drift apart.
	void CopyOut(const wxTextCtrl* from);

	// The button's label and the sentence beside it, written together — two places saying whether
	// it is running is two places that can end up saying different things.
	void RefreshRunState();

	enum {
		ID_McpEnabled = wxID_HIGHEST + 700,
		ID_McpAddress,
		ID_McpPort,
		ID_McpCopyCommand,
		ID_McpCopyBlock,
		ID_McpRun,
	};

	wxCheckBox* m_enabled     = nullptr;
	wxTextCtrl* m_addressCtrl = nullptr;
	wxTextCtrl* m_portCtrl    = nullptr;

	// The easy road and the manual one — a one-line command to paste into an assistant's
	// terminal, and the configuration block for a client that takes a file instead.
	wxTextCtrl* m_commandCtrl  = nullptr;
	wxTextCtrl* m_endpointCtrl = nullptr;

	// Acting on it right now, and saying what it is doing.
	wxButton*     m_runButton  = nullptr;
	wxStaticText* m_stateLabel = nullptr;

	// What the server last reported. Held because the handout is rebuilt whenever the settings
	// change too, and it must not lose the half that only the server can answer.
	wxString m_endpoint;

	ibMcpSettings m_settings;
};

#endif // MCP_SETTINGS_PANEL_H
