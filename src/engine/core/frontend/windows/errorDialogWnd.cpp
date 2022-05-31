////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder community
//	Description : error dialog window
////////////////////////////////////////////////////////////////////////////

#include "errorDialogWnd.h"
#include "frontend/mainFrame.h"

#define DEF_LINENUMBER_ID 0
#define DEF_IMAGE_ID 1

CErrorDialog::CErrorDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	wxBoxSizer* m_bSizerMain = new wxBoxSizer(wxHORIZONTAL);

	m_errorWnd = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(550, 250), 0, wxEmptyString);

	// initialize styles
	m_errorWnd->StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	m_errorWnd->SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < m_errorWnd->GetMarginCount(); margin++)
		m_errorWnd->SetMarginCursor(margin, wxSTC_CURSORARROW);

	m_errorWnd->SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	m_errorWnd->SetMarginWidth(DEF_LINENUMBER_ID, 0);

	// set margin as unused
	m_errorWnd->SetMarginType(DEF_IMAGE_ID, wxSTC_MARGIN_SYMBOL);
	m_errorWnd->SetMarginMask(DEF_IMAGE_ID, ~(1024 | 256 | 512 | 128 | 64 | wxSTC_MASK_FOLDERS));
	m_errorWnd->StyleSetBackground(DEF_IMAGE_ID, *wxWHITE);

	m_errorWnd->SetMarginWidth(DEF_IMAGE_ID, FromDIP(16));
	m_errorWnd->SetMarginSensitive(DEF_IMAGE_ID, true);

	m_bSizerMain->Add(m_errorWnd, 1, wxEXPAND | wxALL, 5);

	wxBoxSizer* m_bSizerButtons = new wxBoxSizer(wxVERTICAL);

	m_buttonCloseWindow = new wxButton(this, wxID_ANY, wxT("Close window"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonCloseWindow, 0, wxALL | wxEXPAND, 5);

	m_buttonGotoDesigner = new wxButton(this, wxID_ANY, wxT("Go to designer"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonGotoDesigner, 0, wxALL | wxEXPAND, 5);

	m_buttonCloseProgram = new wxButton(this, wxID_ANY, wxT("Close program"), wxDefaultPosition, wxDefaultSize, 0);
	m_bSizerButtons->Add(m_buttonCloseProgram, 0, wxALL | wxEXPAND, 5);

	m_bSizerMain->Add(m_bSizerButtons, 0, wxEXPAND, 5);

	this->SetSizer(m_bSizerMain);
	this->Layout();
	m_bSizerMain->Fit(this);

	this->Centre(wxBOTH);

	// Connect Events
	m_buttonCloseProgram->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonCloseProgramClick), NULL, this);
	m_buttonGotoDesigner->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonGotoDesignerClick), NULL, this);
	m_buttonCloseWindow->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonCloseWindowClick), NULL, this);

	CErrorDialog::SetEditorSettings(mainFrame->GetEditorSettings());
	CErrorDialog::SetFontColorSettings(mainFrame->GetFontColorSettings());

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
	entries[0].Set(wxACCEL_CTRL, (int) 'A', idcmdSelectAll);
	entries[1].Set(wxACCEL_CTRL, (int) 'C', idcmdCopy);

	wxAcceleratorTable accel(2, entries);
	m_errorWnd->SetAcceleratorTable(accel);
}

void CErrorDialog::SetEditorSettings(const EditorSettings & settings)
{
	unsigned int m_bIndentationSize = settings.GetIndentSize();

	m_errorWnd->SetIndent(m_bIndentationSize);
	m_errorWnd->SetTabWidth(m_bIndentationSize);

	bool useTabs = settings.GetUseTabs();
	bool showWhiteSpace = settings.GetShowWhiteSpace();

	m_errorWnd->SetUseTabs(useTabs);
	m_errorWnd->SetTabIndents(useTabs);
	m_errorWnd->SetBackSpaceUnIndents(useTabs);
	m_errorWnd->SetViewWhiteSpace(showWhiteSpace);

	m_errorWnd->SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	m_errorWnd->SetMarginWidth(DEF_LINENUMBER_ID, 0);
}

inline wxColour GetInverse(const wxColour& color)
{
	unsigned char r = color.Red();
	unsigned char g = color.Green();
	unsigned char b = color.Blue();

	return wxColour(r ^ 0xFF, g ^ 0xFF, b ^ 0xFF);
}

void CErrorDialog::SetFontColorSettings(const FontColorSettings &settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	m_errorWnd->StyleClearAll();
	m_errorWnd->StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	m_errorWnd->SetSelForeground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).foreColor);
	m_errorWnd->SetSelBackground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Default);

	m_errorWnd->StyleSetFont(wxSTC_C_DEFAULT, font);
	m_errorWnd->StyleSetFont(wxSTC_C_IDENTIFIER, font);

	m_errorWnd->StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_errorWnd->StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_errorWnd->StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Comment);

	m_errorWnd->StyleSetFont(wxSTC_C_COMMENT, font);
	m_errorWnd->StyleSetFont(wxSTC_C_COMMENTLINE, font);
	m_errorWnd->StyleSetFont(wxSTC_C_COMMENTDOC, font);

	m_errorWnd->StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_errorWnd->StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_errorWnd->StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Keyword);

	m_errorWnd->StyleSetFont(wxSTC_C_WORD, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Operator);
	m_errorWnd->StyleSetFont(wxSTC_C_OPERATOR, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_String);

	m_errorWnd->StyleSetFont(wxSTC_C_STRING, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_errorWnd->StyleSetFont(wxSTC_C_STRINGEOL, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_errorWnd->StyleSetFont(wxSTC_C_CHARACTER, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Number);

	m_errorWnd->StyleSetFont(wxSTC_C_NUMBER, font);
	m_errorWnd->StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).foreColor);
	m_errorWnd->StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).backColor);

	m_errorWnd->StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	// Set the caret color as the inverse of the background color so it's always visible.
	m_errorWnd->SetCaretForeground(GetInverse(settings.GetColors(FontColorSettings::DisplayItem_Default).backColor));
}

void CErrorDialog::OnButtonCloseProgramClick(wxCommandEvent & event)
{
	EndModal(3); Destroy(); event.Skip();
}

void CErrorDialog::OnButtonGotoDesignerClick(wxCommandEvent &event)
{
	EndModal(2); Destroy(); event.Skip();
}

void CErrorDialog::OnButtonCloseWindowClick(wxCommandEvent &event)
{
	EndModal(1); Destroy(); event.Skip();
}

CErrorDialog::~CErrorDialog()
{
	// Disconnect Events
	m_buttonCloseProgram->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonCloseProgramClick), NULL, this);
	m_buttonGotoDesigner->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonGotoDesignerClick), NULL, this);
	m_buttonCloseWindow->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CErrorDialog::OnButtonCloseWindowClick), NULL, this);
}
