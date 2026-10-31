#include "debugsettingspanel.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

ibPanelDebugSettings::ibPanelDebugSettings(wxWindow* parent, const ibProtocolNode& shown)
	: wxPanel(parent, wxID_ANY)
{
	m_enableDebugger = new wxCheckBox(this, wxID_ANY, _("Debugging in the current session"));
	m_enableDebugger->Bind(wxEVT_CHECKBOX, &ibPanelDebugSettings::OnEnableDebugger, this);

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_enableDebugger, 0, wxALL, FromDIP(8));
	SetSizer(sizer);

	FillOption(shown);
}

void ibPanelDebugSettings::FillOption(const ibProtocolNode& shown)
{
	m_enableDebugger->SetValue(shown.GetBool(ibProtocol::ibSchema::ibDebugging::Debugging));
	m_enableDebugger->Enable(shown.GetBool(ibProtocol::ibSchema::ibDebugging::Allowed));
}

void ibPanelDebugSettings::OnEnableDebugger(wxCommandEvent& event)
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
