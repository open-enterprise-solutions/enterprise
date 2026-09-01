#include "mcpsettingspanel.h"

#include <wx/clipbrd.h>   // the handout is meant to be taken, not retyped

wxBEGIN_EVENT_TABLE(ibPanelMcpSettings, wxPanel)
	EVT_CHECKBOX(ID_McpEnabled,      ibPanelMcpSettings::OnEnabled)
	EVT_TEXT(    ID_McpAddress,      ibPanelMcpSettings::OnAddressChanged)
	EVT_TEXT(    ID_McpPort,         ibPanelMcpSettings::OnPortChanged)
	EVT_BUTTON(  ID_McpCopyCommand,  ibPanelMcpSettings::OnCopyCommand)
	EVT_BUTTON(  ID_McpCopyBlock,    ibPanelMcpSettings::OnCopyBlock)
	EVT_BUTTON(  ID_McpRun,          ibPanelMcpSettings::OnRun)
wxEND_EVENT_TABLE()

ibPanelMcpSettings::ibPanelMcpSettings(wxWindow* parent, int id, wxPoint pos, wxSize size, int style)
	: wxPanel(parent, id, pos, size, style)
{
	wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* box = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Assistant access (MCP)")), wxVERTICAL);

	m_enabled = new wxCheckBox(this, ID_McpEnabled,
		_("Let an assistant reach this designer"), wxDefaultPosition, wxDefaultSize, 0);
	box->Add(m_enabled, 0, wxALL, 5);

	// SAID PLAINLY, because a person deciding this is deciding how much to hand
	// over — and the honest answer is "everything you can do here".
	wxStaticText* what = new wxStaticText(this, wxID_ANY,
		_("A connected assistant can read the configuration, check syntax and add objects — "
		  "as you, with your rights. It runs only while your session is open."),
		wxDefaultPosition, wxDefaultSize, 0);
	what->Wrap(420);
	box->Add(what, 0, wxALL, 5);

	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 0, 0);
	grid->AddGrowableCol(1);

	grid->Add(new wxStaticText(this, wxID_ANY, _("Address:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_addressCtrl = new wxTextCtrl(this, ID_McpAddress, wxEmptyString);
	grid->Add(m_addressCtrl, 1, wxALL | wxEXPAND, 5);

	grid->Add(new wxStaticText(this, wxID_ANY, _("Port:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_portCtrl = new wxTextCtrl(this, ID_McpPort, wxEmptyString);
	grid->Add(m_portCtrl, 0, wxALL, 5);

	box->Add(grid, 0, wxEXPAND, 5);

	// ⭐ THE SWITCH AND THE BUTTON ARE DIFFERENT QUESTIONS, and both belong here. The checkbox is
	// a STANDING answer — it decides whether this starts by itself next time. The button is NOW:
	// stop it for the afternoon, start it again without closing anything. Somebody who only wants
	// it off today should not have to change what happens every day to get that.
	wxBoxSizer* liveRow = new wxBoxSizer(wxHORIZONTAL);

	m_runButton = new wxButton(this, ID_McpRun, _("Start now"));
	liveRow->Add(m_runButton, 0, wxALL, 5);

	m_stateLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
	liveRow->Add(m_stateLabel, 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	box->Add(liveRow, 0, wxEXPAND, 0);
	page->Add(box, 0, wxALL | wxEXPAND, 5);

	// WHAT TO HAND THE TOOL. Not a setting — an answer, which is why it is
	// read-only and why it is empty until something is actually listening.
	wxStaticBoxSizer* handout = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Give this to your assistant")), wxVERTICAL);

	// ⭐ SETUP IS A LINE TO PASTE, NOT A FORMAT TO UNDERSTAND. Whoever sets this up is not
	// necessarily a programmer — an accountant who bought an assistant subscription is the case
	// this has to work for. Four values and "put them in your configuration" is a task; one
	// command and a Copy button is not.
	//
	// So the page tells them WHAT TO INSTALL and hands them the exact line, in that order.
	// ⭐ THE UNIVERSAL FORM FIRST, AND NAMED AS SUCH. This server speaks plain MCP over HTTP and is
	// tied to no vendor - any client, on any machine, connects the same way. The convenience
	// command below is one PRODUCT's spelling of it, and leading with that would quietly tell
	// somebody using a different assistant that they were in the wrong place.
	wxStaticText* howTo = new wxStaticText(this, wxID_ANY,
		_("Any assistant that speaks MCP can connect, from this machine or another on the "
		  "network. Put this into its server configuration - it is the standard form and needs "
		  "nothing else:"));
	howTo->Wrap(420);
	handout->Add(howTo, 0, wxALL, 5);

	m_endpointCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxSize(-1, 130),
		wxTE_READONLY | wxTE_MULTILINE | wxTE_DONTWRAP);
	handout->Add(m_endpointCtrl, 1, wxALL | wxEXPAND, 5);

	wxButton* copyBlock = new wxButton(this, ID_McpCopyBlock, _("Copy the configuration"));
	handout->Add(copyBlock, 0, wxLEFT | wxBOTTOM, 5);

	// THE SHORTCUT, labelled as belonging to ONE product. Offered because it is genuinely the
	// easiest thing to do when it applies - a line in a terminal beats editing a file - and named
	// so nobody using something else tries it and concludes the server is broken.
	wxStaticText* orCommand = new wxStaticText(this, wxID_ANY,
		_("Using Claude Code? This one line does the same thing:"));
	orCommand->Wrap(420);
	handout->Add(orCommand, 0, wxTOP | wxLEFT | wxRIGHT, 5);

	m_commandCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxSize(-1, 52),
		wxTE_READONLY | wxTE_MULTILINE | wxTE_BESTWRAP);
	handout->Add(m_commandCtrl, 0, wxALL | wxEXPAND, 5);

	wxButton* copyCommand = new wxButton(this, ID_McpCopyCommand, _("Copy the command"));
	handout->Add(copyCommand, 0, wxLEFT | wxBOTTOM, 5);

	// ⚠ SAID LAST, WHERE IT IS ABOUT TO BE ACTED ON. Both of the things above carry the key, and
	// a person is one paste away from handing over everything they can do in this base.
	wxStaticText* warning = new wxStaticText(this, wxID_ANY,
		_("Both contain your access key. Whoever holds it can do here everything you can - put it "
		  "into your own assistant and nowhere else."));
	warning->Wrap(420);
	handout->Add(warning, 0, wxALL, 5);

	page->Add(handout, 1, wxALL | wxEXPAND, 5);

	SetSizer(page);
	Layout();
}

void ibPanelMcpSettings::Initialize()
{
	m_enabled->SetValue(m_settings.m_enabled);
	m_addressCtrl->SetValue(m_settings.m_address);
	m_portCtrl->SetValue(wxString::Format(wxT("%u"), (unsigned)m_settings.m_port));

	UpdateEnabledState();

	// The handout carries the token, which arrives WITH the settings — so it is rebuilt here too
	// and not only when the server reports its address.
	RefreshHandout();
}

void ibPanelMcpSettings::SetSettings(const ibMcpSettings& settings)
{
	m_settings = settings;
	Initialize();
}

const ibMcpSettings& ibPanelMcpSettings::GetSettings() const
{
	return m_settings;
}

void ibPanelMcpSettings::SetEndpoint(const wxString& endpoint)
{
	m_endpoint = endpoint;
	RefreshHandout();

	// The state line quotes the address, and the address arrives HERE - refreshing only on the
	// settings would leave "Running at " with nothing after it the first time the page opens.
	RefreshRunState();
}

void ibPanelMcpSettings::RefreshHandout()
{
	if (m_endpointCtrl == nullptr)
		return;

	if (m_endpoint.IsEmpty()) {
		// NOT RUNNING IS AN ANSWER — and a better one than an address that would be refused.
		const wxString why = m_settings.m_enabled
			? _("The server is not running yet. Close this window with OK and reopen it - the "
				"key appears once the server has actually taken the port.")
			: _("Switched off. Turn it on above to get an address and a key.");

		m_endpointCtrl->SetValue(why);

		if (m_commandCtrl != nullptr)
			m_commandCtrl->SetValue(why);

		return;
	}

	// ⭐ ONE LINE, AND IT IS THE WHOLE SETUP. Not an example to adapt — the address and the key
	// are already in it, so there is nothing left to fill in and nothing to get wrong.
	if (m_commandCtrl != nullptr)
		m_commandCtrl->SetValue(wxString::Format(
			wxT("claude mcp add --transport http oes %s --header \"Authorization: Bearer %s\""),
			m_endpoint, m_settings.m_token));

	// ⭐ THE WHOLE ENTRY, NOT ITS PARTS. Four values a person has to assemble by hand is four
	// chances to assemble them wrongly, and the failure looks identical to the server being down
	// — a refusal with no explanation. This is what the tool's configuration file takes.
	m_endpointCtrl->SetValue(wxString::Format(wxT(
		"{\n"
		"  \"mcpServers\": {\n"
		"    \"oes\": {\n"
		"      \"type\": \"http\",\n"
		"      \"url\": \"%s\",\n"
		"      \"headers\": {\n"
		"        \"Authorization\": \"Bearer %s\"\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"}"), m_endpoint, m_settings.m_token));
}

// ⭐ A BUTTON, BECAUSE SELECTING A KEY BY HAND IS WHERE IT GETS TRUNCATED. A token is 72
// characters of hex with no word boundaries; half of it copied looks exactly like all of it, and
// the failure arrives later as "unauthorized" with nothing to connect it to.
void ibPanelMcpSettings::CopyOut(const wxTextCtrl* from)
{
	if (from == nullptr || m_endpoint.IsEmpty())
		return;   // nothing running means the field holds an explanation, not a handout

	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(from->GetValue()));
		wxTheClipboard->Close();
	}
}

void ibPanelMcpSettings::OnCopyCommand(wxCommandEvent& WXUNUSED(event))
{
	CopyOut(m_commandCtrl);
}

void ibPanelMcpSettings::OnCopyBlock(wxCommandEvent& WXUNUSED(event))
{
	CopyOut(m_endpointCtrl);
}

//---------------------------------------------------------------------------
// Running it, here and now
//---------------------------------------------------------------------------

#include "backend/appData.h"
#include "backend/session/session.h"

void ibPanelMcpSettings::OnRun(wxCommandEvent& WXUNUSED(event))
{
	ibMcpServer* server = ibApplicationData::GetMcpServer();
	if (server == nullptr)
		return;

	if (server->IsRunning()) {
		server->Stop();
		RefreshRunState();
		return;
	}

	// ⭐ IT STARTS ON WHAT THE PAGE SAYS, not on what it was told last time. Somebody who just
	// changed the port and pressed Start means the new port; starting on the old one and showing
	// the new one in the field above is the page lying about itself.
	//
	// Safe here BY CONSTRUCTION: Configure refuses to move a RUNNING server, and this branch only
	// runs when it is stopped.
	server->Configure(m_settings);

	// IN THE NAME OF THIS SESSION. Everything the assistant then does, it does as the person who
	// pressed this - same rights, same configuration - and it stops when they close the designer.
	wxString refusal;

	if (!server->Start(ibSession::Current(), refusal)) {
		// ⚠ ON THE PAGE, NOT IN A BOX. The refusal names the reason - switched off, port taken -
		// and a box would have to be dismissed before the setting it talks about could be
		// changed. Here it stays next to the control that fixes it.
		if (m_stateLabel != nullptr)
			m_stateLabel->SetLabel(refusal);

		Layout();
		return;
	}

	// ⭐ AND THE PAGE CATCHES UP WITH THE SERVER. Start may have minted a token; reading the
	// settings back is what puts the real one into the command below, instead of the copy this
	// page was handed when it opened.
	m_settings = server->GetSettings();

	SetEndpoint(server->GetEndpoint());
	RefreshRunState();
}

// What is true RIGHT NOW, said in one place so the button's label and the sentence beside it
// cannot disagree.
void ibPanelMcpSettings::RefreshRunState()
{
	const ibMcpServer* server = ibApplicationData::GetMcpServer();

	const bool running = server != nullptr && server->IsRunning();

	if (m_runButton != nullptr)
		m_runButton->SetLabel(running ? _("Stop now") : _("Start now"));

	if (m_stateLabel != nullptr)
		m_stateLabel->SetLabel(running
			? wxString::Format(_("Running at %s"), m_endpoint)
			: (m_settings.m_enabled
				? _("Not running. It starts by itself next time you open the designer.")
				: _("Not running, and switched off above - it will not start by itself.")));

	Layout();
}

void ibPanelMcpSettings::UpdateEnabledState()
{
	const bool on = m_settings.m_enabled;
	m_addressCtrl->Enable(on);
	m_portCtrl->Enable(on);

	// ⚠ THE BUTTON IS NOT GREYED WITH THEM. Stopping a RUNNING server must stay possible even
	// after somebody unticks the box — that combination is exactly "switch it off now and keep it
	// off", and greying the only control that does the first half would strand it running.
	RefreshRunState();
}

void ibPanelMcpSettings::OnEnabled(wxCommandEvent& event)
{
	m_settings.m_enabled = m_enabled->GetValue();
	UpdateEnabledState();
	event.Skip();
}

void ibPanelMcpSettings::OnAddressChanged(wxCommandEvent& event)
{
	m_settings.m_address = m_addressCtrl->GetValue();
	event.Skip();
}

void ibPanelMcpSettings::OnPortChanged(wxCommandEvent& event)
{
	// A PORT THAT IS NOT A NUMBER LEAVES THE OLD ONE STANDING. Half-typed text
	// passes through here on every keystroke, so refusing loudly would fight the
	// person typing; the value is checked again where it is used, and the server
	// says plainly when it cannot take it.
	unsigned long port = 0;
	if (m_portCtrl->GetValue().ToULong(&port) && port > 0 && port <= 65535)
		m_settings.m_port = (unsigned short)port;

	event.Skip();
}
