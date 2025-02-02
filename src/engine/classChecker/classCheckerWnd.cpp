#include "classCheckerWnd.h"

CFrameClassChecker::CFrameClassChecker(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxSize(500, 130), wxSize(500, 130));
	this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
	this->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizerMain;
	bSizerMain = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sbSizerName;
	sbSizerName = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Name")), wxHORIZONTAL);

	m_staticText_clsid = new wxStaticText(sbSizerName->GetStaticBox(), wxID_ANY, _("class identifier"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_clsid->Wrap(-1);
	sbSizerName->Add(m_staticText_clsid, 1, wxALIGN_CENTER_VERTICAL, 5);

	m_textCtrlCode = new wxTextCtrl(sbSizerName->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sbSizerName->Add(m_textCtrlCode, 1, wxALIGN_CENTER_VERTICAL, 5);


	bSizerMain->Add(sbSizerName, 0, wxEXPAND, 5);

	wxStaticBoxSizer* sbSizerOutput;
	sbSizerOutput = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Output")), wxHORIZONTAL);

	m_staticText_output = new wxStaticText(sbSizerOutput->GetStaticBox(), wxID_ANY, _("result"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_output->Wrap(-1);
	sbSizerOutput->Add(m_staticText_output, 1, wxALIGN_CENTER_VERTICAL, 5);

	m_textCtrlResult = new wxTextCtrl(sbSizerOutput->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sbSizerOutput->Add(m_textCtrlResult, 1, wxALIGN_CENTER_VERTICAL, 5);

#ifdef __WXGTK__
	if (!m_textCtrlResult->HasFlag(wxTE_MULTILINE))
	{
		m_textCtrlResult->SetMaxLength(8);
	}
#else
	m_textCtrlResult->SetMaxLength(8);
#endif


	bSizerMain->Add(sbSizerOutput, 0, wxEXPAND, 5);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	// Connect Events
	m_textCtrlCode->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(CFrameClassChecker::ClsidOnText), nullptr, this);
	m_textCtrlResult->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(CFrameClassChecker::OutputOnText), nullptr, this);

	m_textCtrlResult->SetValue("VL_TEST1");
}

CFrameClassChecker::~CFrameClassChecker()
{
}

#include "backend/backend_core.h"

void CFrameClassChecker::ClsidOnText(wxCommandEvent& event)
{
	class_identifier_t clsid = 0;
	if (m_textCtrlCode->GetValue().ToULongLong(&clsid)) {
		const wxString& str_clsid = clsid_to_string(clsid);
		m_textCtrlResult->SetValue(stringUtils::TrimAll(str_clsid));
	}
	event.Skip();
}

void CFrameClassChecker::OutputOnText(wxCommandEvent& event)
{
	const class_identifier_t& clsid = string_to_clsid(m_textCtrlResult->GetValue());
	wxString str_clsid;
	str_clsid << clsid;
	m_textCtrlCode->SetValue(str_clsid);
	event.Skip();
}
