#include "docViewText.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "wx/filedlg.h"   // wxFileSelector - the text box's own Save as

// ----------------------------------------------------------------------------
// ibFrontendTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFrontendTextEditView, ibFrontendView);

wxBEGIN_EVENT_TABLE(ibFrontendTextEditView, ibFrontendView)
EVT_MENU(wxID_COPY, ibFrontendTextEditView::OnCopy)
EVT_MENU(wxID_PASTE, ibFrontendTextEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, ibFrontendTextEditView::OnSelectAll)
wxEND_EVENT_TABLE()

bool ibFrontendTextEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	m_textEditor = new ibTextEditor(doc, m_viewFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_textEditor->SetEditorSettings(mainFrame->GetEditorSettings());
	m_textEditor->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_textEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(doc, flags);
}

void ibFrontendTextEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibFrontendTextEditView::OnClose(bool deleteWindow)
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

wxPrintout* ibFrontendTextEditView::OnCreatePrintout()
{
	return new ibTextEditorPrintout(m_textEditor, m_viewDocument->GetTitle());
}

void ibFrontendTextEditView::OnFind(wxFindDialogEvent& event)
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
// ibFrontendTextDocument: ibFrontendDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibFrontendTextDocument, ibFrontendDocument);

wxCommandProcessor* ibFrontendTextDocument::OnCreateCommandProcessor()
{
	ibTextCommandProcessor* commandProcessor = new ibTextCommandProcessor(GetTextCtrl());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

ibTextEditor* ibFrontendTextDocument::GetTextCtrl() const
{
	ibFrontendView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibFrontendTextEditView)->GetText() : nullptr;
}

// ----------------------------------------------------------------------------
// ibFrontendTextFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFrontendTextFileDocument, ibFrontendTextDocument);

bool ibFrontendTextFileDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibFrontendDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibFrontendTextFileDocument::DoSaveDocument(const wxString& filename)
{
	return GetTextCtrl()->SaveFile(filename);
}

bool ibFrontendTextFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetTextCtrl()->LoadFile(filename))
		return false;

	Modify(false);
	return true;
}

// ----------------------------------------------------------------------------
// ibTextBoxDocument / View: the document a form's text box holds, and its view
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibTextBoxDocument, ibFrontendTextFileDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibTextBoxView, ibFrontendTextEditView);

ibTextBoxDocument::ibTextBoxDocument() : ibFrontendTextFileDocument()
{
	SetTitle(_("Text document"));
}

bool ibTextBoxDocument::SaveAs()
{
	const wxString fileName = wxFileSelector(_("Save As"),
		wxEmptyString,
		wxFileNameFromPath(GetFilename()),
		wxEmptyString,
		GetSaveFilter(),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT,
		GetDocumentWindow());

	if (fileName.empty())
		return false; // cancelled by user

	return DoSaveDocument(fileName);
}

bool ibTextBoxView::OnCreate(ibFrontendDocument* doc, long flags)
{
	if (!ibFrontendTextEditView::OnCreate(doc, flags))
		return false;

	// The document's undo drives this editor, so it is made with it — as the manager makes a document's —
	// and dropped in OnClose.
	delete doc->GetCommandProcessor();
	doc->SetCommandProcessor(doc->OnCreateCommandProcessor());

	return true;
}

bool ibTextBoxView::OnClose(bool WXUNUSED(deleteWindow))
{
	// Not the base's close — see ibSpreadsheetGridBoxView::OnClose.
	if (ibFrontendDocument* const doc = GetDocument()) {
		delete doc->GetCommandProcessor();
		doc->SetCommandProcessor(nullptr);
	}

	m_textEditor = nullptr;
	SetFrame(nullptr);
	return true;
}