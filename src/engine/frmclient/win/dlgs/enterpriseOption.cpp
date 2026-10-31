#include "enterpriseOption.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/theme/luna_tabart.h"
#include "protocol/protocol.h"

void ibDialogEnterpriseOption::FillOption(const ibProtocolNode& shown)
{
	m_enableDebugger->SetValue(shown.GetBool(ibProtocol::ibSchema::ibDebugging::Debugging));
	m_enableDebugger->Enable(shown.GetBool(ibProtocol::ibSchema::ibDebugging::Allowed));
}

////////////////////////////////////////////////////////////////////////////////////////////////

ibDialogEnterpriseOption::ibDialogEnterpriseOption(wxWindow* parent, const ibProtocolNode& shown, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_mainNotebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP |
		wxAUI_NB_TAB_SPLIT |
		wxAUI_NB_TAB_MOVE |
		wxAUI_NB_SCROLL_BUTTONS
	);

	m_mainNotebook->SetArtProvider(new wxAuiLunaTabArt());
	m_generalPanel = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_mainNotebook->AddPage(m_generalPanel, _("General"), false, wxNullBitmap);
	m_systemPanel = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_mainNotebook->AddPage(m_systemPanel, _("System"), true, wxNullBitmap);
	mainSizer->Add(m_mainNotebook, 1, wxEXPAND | wxALL, FromDIP(5));
	m_enableDebugger = new wxCheckBox(m_systemPanel, wxID_ANY, _("Debugging in the current session"), wxDefaultPosition, wxDefaultSize, 0);
	m_enableDebugger->Bind(wxEVT_CHECKBOX, &ibDialogEnterpriseOption::OnEnableDebugger, this);

	m_mainNotebook->SetSelection(2);

	this->SetSizer(mainSizer);
	this->Layout();
	this->Centre(wxBOTH);

	FillOption(shown);
}

void ibDialogEnterpriseOption::OnEnableDebugger(wxCommandEvent& event)
{
	// Switched on the server, as the desktop's started or shut its debug server — and the box asked again, as it is
	// there now: a start that did not take leaves it as it was.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame != nullptr) {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Debugging));
		params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(m_enableDebugger->GetValue()
			? ibProtocol::ibSchema::ibCommand::Start : ibProtocol::ibSchema::ibCommand::Stop));
		frame->Call(ibProtocol::ibMethod::Schema, params, answer);

		ibProtocolNode shown;
		params = ibProtocolNode();
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Debugging));
		if (frame->Call(ibProtocol::ibMethod::Schema, params, shown))
			FillOption(shown);
	}

	event.Skip();
}
