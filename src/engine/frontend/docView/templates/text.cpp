#include "text.h"
#include "frontend/mainFrame/mainFrame.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CTextEditView, CMetaView);

wxBEGIN_EVENT_TABLE(CTextEditView, CMetaView)
EVT_MENU(wxID_COPY, CTextEditView::OnCopy)
EVT_MENU(wxID_PASTE, CTextEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CTextEditView::OnSelectAll)
wxEND_EVENT_TABLE()

void CTextEditView::SetEditorSettings(const EditorSettings & settings)
{
	unsigned int m_bIndentationSize = settings.GetIndentSize();

	m_textEditor->SetIndent(m_bIndentationSize);
	m_textEditor->SetTabWidth(m_bIndentationSize);

	bool useTabs = settings.GetUseTabs();
	bool showWhiteSpace = settings.GetShowWhiteSpace();

	m_textEditor->SetUseTabs(useTabs);
	m_textEditor->SetTabIndents(useTabs);
	m_textEditor->SetBackSpaceUnIndents(useTabs);
	m_textEditor->SetViewWhiteSpace(showWhiteSpace);
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

	m_textEditor->StyleClearAll();
	m_textEditor->StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	m_textEditor->SetSelForeground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).foreColor);
	m_textEditor->SetSelBackground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Default);

	m_textEditor->StyleSetFont(wxSTC_C_DEFAULT, font);
	m_textEditor->StyleSetFont(wxSTC_C_IDENTIFIER, font);

	m_textEditor->StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_textEditor->StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	m_textEditor->StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Comment);

	m_textEditor->StyleSetFont(wxSTC_C_COMMENT, font);
	m_textEditor->StyleSetFont(wxSTC_C_COMMENTLINE, font);
	m_textEditor->StyleSetFont(wxSTC_C_COMMENTDOC, font);

	m_textEditor->StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_textEditor->StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	m_textEditor->StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Keyword);

	m_textEditor->StyleSetFont(wxSTC_C_WORD, font);
	m_textEditor->StyleSetForeground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Operator);
	m_textEditor->StyleSetFont(wxSTC_C_OPERATOR, font);
	m_textEditor->StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_String);

	m_textEditor->StyleSetFont(wxSTC_C_STRING, font);
	m_textEditor->StyleSetForeground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_textEditor->StyleSetFont(wxSTC_C_STRINGEOL, font);
	m_textEditor->StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	m_textEditor->StyleSetFont(wxSTC_C_CHARACTER, font);
	m_textEditor->StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Number);

	m_textEditor->StyleSetFont(wxSTC_C_NUMBER, font);
	m_textEditor->StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).foreColor);
	m_textEditor->StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).backColor);

	m_textEditor->StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	// Set the caret color as the inverse of the background color so it's always visible.
	m_textEditor->SetCaretForeground(GetInverse(settings.GetColors(FontColorSettings::DisplayItem_Default).backColor));
}

bool CTextEditView::OnCreate(CMetaDocument *doc, long flags)
{	
	m_textEditor = new wxStyledTextCtrl(m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize,
		wxBORDER_THEME);

	//Set margin cursor
	for (int margin = 0; margin < m_textEditor->GetMarginCount(); margin++)
		m_textEditor->SetMarginCursor(margin, wxSTC_CURSORARROW);

	m_textEditor->SetReadOnly(flags == wxDOC_READONLY);

	CTextEditView::SetEditorSettings(mainFrame->GetEditorSettings());
	CTextEditView::SetFontColorSettings(mainFrame->GetFontColorSettings());

	return CMetaView::OnCreate(doc, flags);
}

void CTextEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CTextEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (CMetaView::OnClose(deleteWindow))
		return m_textEditor->Destroy();

	return false;
}

//#include "frontend/codeEditor/codeEditorCtrlPrintOut.h"

wxPrintout* CTextEditView::OnCreatePrintout()
{
	return nullptr; //new CCodeEditorPrintout(m_textEditor, this->GetViewName());
}

#include "backend/metaData.h"
#include "backend/systemManager/systemManager.h"

void CTextEditView::OnFind(wxFindDialogEvent& event)
{
	int wxflags = event.GetFlags();
	int sciflags = 0;
	if ((wxflags & wxFR_WHOLEWORD) != 0)
	{
		sciflags |= wxSTC_FIND_WHOLEWORD;
	}
	if ((wxflags & wxFR_MATCHCASE) != 0)
	{
		sciflags |= wxSTC_FIND_MATCHCASE;
	}
	int result;
	if ((wxflags & wxFR_DOWN) != 0)
	{
		m_textEditor->SetSelectionStart(m_textEditor->GetSelectionEnd());
		m_textEditor->SearchAnchor();
		result = m_textEditor->SearchNext(sciflags, event.GetFindString());
	}
	else
	{
		m_textEditor->SetSelectionEnd(m_textEditor->GetSelectionStart());
		m_textEditor->SearchAnchor();
		result = m_textEditor->SearchPrev(sciflags, event.GetFindString());
	}
	if (wxSTC_INVALID_POSITION == result)
	{
		wxMessageBox(wxString::Format(_("\"%s\" not found!"), event.GetFindString().c_str()), _("Not Found!"), wxICON_ERROR, (wxWindow*)event.GetClientData());
	}
	else
	{
		m_textEditor->EnsureCaretVisible();
	}
}

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CTextDocument, CMetaDocument);

bool CTextDocument::OnCreate(const wxString& path, long flags)
{
	if (!CMetaDocument::OnCreate(path, flags))
		return false;

	class CModuleCommandProcessor : public wxCommandProcessor {
		wxStyledTextCtrl* m_codeEditor;
	public:

		CModuleCommandProcessor(wxStyledTextCtrl* codeEditor) :
			wxCommandProcessor(), m_codeEditor(codeEditor) {
		}

		virtual bool Undo() override {
			m_codeEditor->Undo();
			return true;
		}

		virtual bool Redo() override {
			m_codeEditor->Redo();
			return true;
		}

		virtual bool CanUndo() const override {
			return m_codeEditor->CanUndo();
		}

		virtual bool CanRedo() const override {
			return m_codeEditor->CanRedo();
		}
	};

	CModuleCommandProcessor *commandProcessor = new CModuleCommandProcessor(GetTextCtrl());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();

	wxDocument::SetCommandProcessor(commandProcessor);

	// subscribe to changes in the text control to update the document state
	// when it's modified
	GetTextCtrl()->Connect
	(
		wxEVT_STC_CHANGE,
		wxCommandEventHandler(CTextDocument::OnTextChange),
		nullptr,
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
	return CMetaDocument::IsModified() || (wnd && wnd->IsModified());
}

void CTextDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);

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
	return view ? wxDynamicCast(view, CTextEditView)->GetText() : nullptr;
}