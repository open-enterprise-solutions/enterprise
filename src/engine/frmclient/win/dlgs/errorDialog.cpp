////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : error dialog window
////////////////////////////////////////////////////////////////////////////

#include "errorDialog.h"

#include "frmclient/backend/backend_picture.h"
#include "core/picturePredefined.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

#define DEF_LINENUMBER_ID 0
#define DEF_IMAGE_ID 1

ibDialogError::ibDialogError(ibFrontendMainFrame* parent, wxWindowID id,
	const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	wxBoxSizer* bSizerMain = new wxBoxSizer(wxHORIZONTAL);

	m_errorOutput = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(550, 250), 0, wxEmptyString);

	// initialize styles
	m_errorOutput->StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	m_errorOutput->SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < m_errorOutput->GetMarginCount(); margin++)
		m_errorOutput->SetMarginCursor(margin, wxSTC_CURSORARROW);

	m_errorOutput->SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	m_errorOutput->SetMarginWidth(DEF_LINENUMBER_ID, 0);

	// set margin as unused
	m_errorOutput->SetMarginType(DEF_IMAGE_ID, wxSTC_MARGIN_SYMBOL);
	m_errorOutput->SetMarginMask(DEF_IMAGE_ID, ~(1024 | 256 | 512 | 128 | 64 | wxSTC_MASK_FOLDERS));
	m_errorOutput->StyleSetBackground(DEF_IMAGE_ID, *wxWHITE);

	m_errorOutput->SetMarginWidth(DEF_IMAGE_ID, FromDIP(16));
	m_errorOutput->SetMarginSensitive(DEF_IMAGE_ID, true);

	bSizerMain->Add(m_errorOutput, 1, wxEXPAND | wxALL, FromDIP(5));

	wxBoxSizer* m_bSizerButtons = new wxBoxSizer(wxVERTICAL);

	m_buttonCloseWindow = new wxButton(this, wxID_ANY, _("Close window"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonCloseWindow, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonGotoDesigner = new wxButton(this, wxID_ANY, _("Go to designer"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonGotoDesigner, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonCloseProgram = new wxButton(this, wxID_ANY, _("Close program"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonCloseProgram, 0, wxALL | wxEXPAND, FromDIP(5));

	bSizerMain->Add(m_bSizerButtons, 0, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(bSizerMain);
	wxDialog::Layout();
	bSizerMain->Fit(this);

	wxDialog::Centre(wxBOTH);

	// Connect Events
	m_buttonCloseProgram->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogError::OnButtonCloseProgramClick), nullptr, this);
	m_buttonGotoDesigner->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogError::OnButtonGotoDesignerClick), nullptr, this);
	m_buttonCloseWindow->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogError::OnButtonCloseWindowClick), nullptr, this);

	// The report in the code's font and colours, without line numbers.
	parent->GetEditorSettings().Apply(*m_errorOutput);
	parent->GetFontColorSettings().Apply(*m_errorOutput);
	m_errorOutput->SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	m_errorOutput->SetMarginWidth(DEF_LINENUMBER_ID, 0);

	/** Enumeration of commands and child windows. */
	enum
	{
		idcmdUndo = 10,
		idcmdRedo = 11,
		idcmdCut = 12,
		idcmdCopy = 13,
		idcmdPaste = 14,
		idcmdDelete = 15,
		idcmdSelectAll = 16
	};

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'A', idcmdSelectAll);
	entries[1].Set(wxACCEL_CTRL, (int)'C', idcmdCopy);

	wxAcceleratorTable accel(2, entries);
	m_errorOutput->SetAcceleratorTable(accel);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picErrorCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

// The act chosen is what ShowModal answers. The dialog is the caller's (on its stack), so it is ended, not destroyed.

void ibDialogError::OnButtonCloseProgramClick(wxCommandEvent& event)
{
	EndModal(static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::CloseProgram)); event.Skip();
}

void ibDialogError::OnButtonGotoDesignerClick(wxCommandEvent& event)
{
	EndModal(static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::GotoDesigner)); event.Skip();
}

void ibDialogError::OnButtonCloseWindowClick(wxCommandEvent& event)
{
	EndModal(static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::CloseWindow)); event.Skip();
}
