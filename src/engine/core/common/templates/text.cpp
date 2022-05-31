#include "text.h"
#include "module_cmd.h"
#include "frontend/mainFrame.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CTextEditView, CView);

wxBEGIN_EVENT_TABLE(CTextEditView, CView)
EVT_MENU(wxID_COPY, CTextEditView::OnCopy)
EVT_MENU(wxID_PASTE, CTextEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CTextEditView::OnSelectAll)
wxEND_EVENT_TABLE()

void CTextEditView::SetEditorSettings(const EditorSettings & settings)
{
	unsigned int m_bIndentationSize = settings.GetIndentSize();

	m_text->SetIndent(m_bIndentationSize);
	m_text->SetTabWidth(m_bIndentationSize);

	bool useTabs = settings.GetUseTabs();
	bool showWhiteSpace = settings.GetShowWhiteSpace();

	m_text->SetUseTabs(useTabs);
	m_text->SetTabIndents(useTabs);
	m_text->SetBackSpaceUnIndents(useTabs);
	m_text->SetViewWhiteSpace(showWhiteSpace);
}

inline wxColour GetInverse(const wxColour& color)
{
	unsigned char r = color.Red();
	unsigned char g = color.Green();
	unsigned char b = color.Blue();

	return wxColour(r ^ 0xFF, g ^ 0xFF, b ^ 0xFF);
}

void CTextEditView::SetFontColorSettings(const FontColorSettings &settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	m_text->StyleClearAll();
	m_text->StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	m_text->SetSelForeground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).foreColor);
	m_text->SetSelBackground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Default);

	m_text->StyleSetFont(wxSTC_C_DEFAULT, font);
	m_text->StyleSetFont(wxSTC_C_IDENTIFIER, font);

	m_text->StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_text->StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_text->StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_text->StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_text->StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_text->StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Comment);

	m_text->StyleSetFont(wxSTC_C_COMMENT, font);
	m_text->StyleSetFont(wxSTC_C_COMMENTLINE, font);
	m_text->StyleSetFont(wxSTC_C_COMMENTDOC, font);

	m_text->StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_text->StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_text->StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_text->StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_text->StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_text->StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Keyword);

	m_text->StyleSetFont(wxSTC_C_WORD, font);
	m_text->StyleSetForeground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).foreColor);
	m_text->StyleSetBackground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Operator);
	m_text->StyleSetFont(wxSTC_C_OPERATOR, font);
	m_text->StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).foreColor);
	m_text->StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_String);

	m_text->StyleSetFont(wxSTC_C_STRING, font);
	m_text->StyleSetForeground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_text->StyleSetBackground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_text->StyleSetFont(wxSTC_C_STRINGEOL, font);
	m_text->StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_text->StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_text->StyleSetFont(wxSTC_C_CHARACTER, font);
	m_text->StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_text->StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Number);

	m_text->StyleSetFont(wxSTC_C_NUMBER, font);
	m_text->StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).foreColor);
	m_text->StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).backColor);

	m_text->StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	// Set the caret color as the inverse of the background color so it's always visible.
	m_text->SetCaretForeground(GetInverse(settings.GetColors(FontColorSettings::DisplayItem_Default).backColor));
}

bool CTextEditView::OnCreate(CDocument *doc, long flags)
{	
	m_text = new wxStyledTextCtrl(m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize,
		wxBORDER_THEME);

	//Set margin cursor
	for (int margin = 0; margin < m_text->GetMarginCount(); margin++)
		m_text->SetMarginCursor(margin, wxSTC_CURSORARROW);

	m_text->SetReadOnly(flags == wxDOC_READONLY);

	CTextEditView::SetEditorSettings(mainFrame->GetEditorSettings());
	CTextEditView::SetFontColorSettings(mainFrame->GetFontColorSettings());

	return CView::OnCreate(doc, flags);
}

void CTextEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CTextEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CTextDocument, CDocument);

bool CTextDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	wxStyledTextCtrl* m_context = GetTextCtrl();
	CModuleCommandProcessor *commandProcessor = new CModuleCommandProcessor(m_context);

	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();

	wxDocument::SetCommandProcessor(commandProcessor);

	// subscribe to changes in the text control to update the document state
	// when it's modified
	GetTextCtrl()->Connect
	(
		wxEVT_STC_CHANGE,
		wxCommandEventHandler(CTextDocument::OnTextChange),
		NULL,
		this
	);

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CTextDocument::DoSaveDocument(const wxString& filename)
{
	return GetTextCtrl()->SaveFile(filename);
}

bool CTextDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetTextCtrl()->LoadFile(filename))
		return false;

	return true;
}

bool CTextDocument::IsModified() const
{
	wxStyledTextCtrl* wnd = GetTextCtrl();
	return CDocument::IsModified() || (wnd && wnd->IsModified());
}

void CTextDocument::Modify(bool modified)
{
	CDocument::Modify(modified);

	wxStyledTextCtrl* wnd = GetTextCtrl();
	if (wnd && !modified) {
		wnd->DiscardEdits();
	}
}

void CTextDocument::OnTextChange(wxCommandEvent& event)
{
	Modify(true);
	event.Skip();
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CTextEditDocument, CTextDocument);

wxStyledTextCtrl* CTextEditDocument::GetTextCtrl() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CTextEditView)->GetText() : NULL;
}