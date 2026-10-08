#include "docViewHelp.h"
#include "frmclient/mainFrame/mainFrame.h"

// ----------------------------------------------------------------------------
// ibFrontendHelpEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFrontendHelpEditView, ibFrontendView);

wxBEGIN_EVENT_TABLE(ibFrontendHelpEditView, ibFrontendView)
EVT_MENU(wxID_COPY, ibFrontendHelpEditView::OnCopy)
EVT_MENU(wxID_PASTE, ibFrontendHelpEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, ibFrontendHelpEditView::OnSelectAll)
wxEND_EVENT_TABLE()

bool ibFrontendHelpEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	m_textEditor = new ibTextEditor(doc, m_viewFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_textEditor->SetEditorSettings(mainFrame->GetEditorSettings());
	m_textEditor->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_textEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(doc, flags);
}

void ibFrontendHelpEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibFrontendHelpEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibFrontendView::OnClose(deleteWindow)) {
		
		m_textEditor->Freeze();
		
		m_textEditor->Destroy();
		m_textEditor = nullptr;

		return true;
	}

	return false;
}

#include "frmclient/win/editor/textEditor/textEditorPrintOut.h"

wxPrintout* ibFrontendHelpEditView::OnCreatePrintout()
{
	return new ibTextEditorPrintout(m_textEditor, m_viewDocument->GetTitle());
}

void ibFrontendHelpEditView::OnFind(wxFindDialogEvent& event)
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
// ibFrontendHelpDocument: ibFrontendDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibFrontendHelpDocument, ibFrontendDocument);

wxCommandProcessor* ibFrontendHelpDocument::OnCreateCommandProcessor()
{
	ibTextCommandProcessor* commandProcessor = new ibTextCommandProcessor(GetTextCtrl());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

ibTextEditor* ibFrontendHelpDocument::GetTextCtrl() const
{
	ibFrontendView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibFrontendHelpEditView)->GetText() : nullptr;
}

// ----------------------------------------------------------------------------
// ibFrontendHelpFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFrontendHelpFileDocument, ibFrontendHelpDocument);

bool ibFrontendHelpFileDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibFrontendDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibFrontendHelpFileDocument::DoSaveDocument(const wxString& filename)
{
	return GetTextCtrl()->SaveFile(filename);
}

bool ibFrontendHelpFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetTextCtrl()->LoadFile(filename))
		return false;

	Modify(false);
	return true;
}