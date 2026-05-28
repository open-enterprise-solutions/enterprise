#include "docViewModuleEditor.h"
#include "frontend/mainFrame/mainFrame.h"

enum {
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,
	wxID_SYNTAX_CONTROL,
	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS,
	wxID_FORMAT_CODE,
	wxID_INCREASE_INDENT,
	wxID_DECREASE_INDENT
};

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibModuleEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibModuleEditView, ibMetaView)
EVT_MENU(wxID_COPY, ibModuleEditView::OnCopy)
EVT_MENU(wxID_PASTE, ibModuleEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, ibModuleEditView::OnSelectAll)
EVT_FIND(wxID_ANY, ibModuleEditView::OnFind)
EVT_FIND_NEXT(wxID_ANY, ibModuleEditView::OnFind)

EVT_MENU(wxID_ADD_COMMENTS, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_REMOVE_COMMENTS, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_SYNTAX_CONTROL, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_GOTOLINE, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_PROCEDURES_FUNCTIONS, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_FORMAT_CODE, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_INCREASE_INDENT, ibModuleEditView::OnMenuEvent)
EVT_MENU(wxID_DECREASE_INDENT, ibModuleEditView::OnMenuEvent)

wxEND_EVENT_TABLE()

bool ibModuleEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_codeEditor = new ibCodeEditorDesigner(doc, m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_codeEditor->SetReadOnly(flags == ibDOC_READONLY);
	m_codeEditor->SetSTCFocus(true);

	return ibMetaView::OnCreate(doc, flags)
		&& m_codeEditor->LoadModule();
}

void ibModuleEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_codeEditor->ActivateEditor();
}

void ibModuleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

void ibModuleEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_codeEditor != nullptr)
		m_codeEditor->RefreshEditor();
}

bool ibModuleEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		m_codeEditor->Freeze();

		m_codeEditor->Destroy();
		m_codeEditor = nullptr;

		return true;
	}

	return false;
}

#include "frontend/win/editor/codeEditor/codeEditorPrintOut.h"

wxPrintout* ibModuleEditView::OnCreatePrintout()
{
	return new ibCodeEditorPrintout(m_codeEditor, m_viewDocument->GetTitle());
}

#include "frontend/artProvider/artProvider.h"

void ibModuleEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (m_codeEditor == nullptr)
		return;

	if (!toolbar->GetToolCount()) {
		toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxArtProvider::GetBitmapBundle(wxART_ADD_COMMENT, wxART_DOC_MODULE), _("Add"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_ADD_COMMENTS, m_codeEditor->IsEditable());
		toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxArtProvider::GetBitmapBundle(wxART_REMOVE_COMMENT, wxART_DOC_MODULE), _("Remove"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_codeEditor->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_SYNTAX_CONTROL, _("Syntax control"), wxArtProvider::GetBitmapBundle(wxART_SYNTAX_CONTROL, wxART_DOC_MODULE), _("Syntax"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_SYNTAX_CONTROL, m_codeEditor->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxArtProvider::GetBitmapBundle(wxART_GOTO_LINE, wxART_DOC_MODULE), _("Goto"), wxItemKind::wxITEM_NORMAL);
		toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxArtProvider::GetBitmapBundle(wxART_PROC_AND_FUNC, wxART_DOC_MODULE), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_FORMAT_CODE, _("Format selection"), wxArtProvider::GetBitmapBundle(wxART_FORMAT_CODE, wxART_DOC_MODULE), _("Format"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_FORMAT_CODE, m_codeEditor->IsEditable());
		toolbar->AddTool(wxID_INCREASE_INDENT, _("Increase indent"), wxArtProvider::GetBitmapBundle(wxART_GO_FORWARD, wxART_TOOLBAR, wxSize(16, 16)), _("Indent"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_INCREASE_INDENT, m_codeEditor->IsEditable());
		toolbar->AddTool(wxID_DECREASE_INDENT, _("Decrease indent"), wxArtProvider::GetBitmapBundle(wxART_GO_BACK, wxART_TOOLBAR, wxSize(16, 16)), _("Unindent"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_DECREASE_INDENT, m_codeEditor->IsEditable());
	}
}

#include "frontend/win/dlgs/lineInput/lineInput.h"
#include "frontend/win/dlgs/functionSearcher/functionSearcher.h"

void ibModuleEditView::OnMenuEvent(wxCommandEvent& event)
{
	if (event.GetId() == wxID_ADD_COMMENTS) {
		int nStartLine, nEndLine;
		m_codeEditor->GetSelection(&nStartLine, &nEndLine);
		for (int line = m_codeEditor->LineFromPosition(nStartLine); line <= m_codeEditor->LineFromPosition(nEndLine); line++) {
			m_codeEditor->Replace(m_codeEditor->PositionFromLine(line), m_codeEditor->PositionFromLine(line), "//");
		}
	}
	else if (event.GetId() == wxID_REMOVE_COMMENTS) {
		int nStartLine, nEndLine;
		m_codeEditor->GetSelection(&nStartLine, &nEndLine);
		for (int line = m_codeEditor->LineFromPosition(nStartLine); line <= m_codeEditor->LineFromPosition(nEndLine); line++) {
			int startPos = m_codeEditor->PositionFromLine(line);
			wxString sLine = m_codeEditor->GetLineRaw(line);
			for (unsigned int i = 0; i < sLine.length(); i++) {
				if (sLine[i] == '/'
					&& (i + 1 < sLine.length() && sLine[i + 1] == '/')) {
					m_codeEditor->Replace(startPos + i, startPos + i + 2, wxEmptyString); break;
				}
			}
		}
	}
	else if (event.GetId() == wxID_SYNTAX_CONTROL) {
		m_codeEditor->SyntaxControl();
	}
	else if (event.GetId() == wxID_GOTOLINE) {
		m_codeEditor->ShowGotoLine();
	}
	else if (event.GetId() == wxID_PROCEDURES_FUNCTIONS) {
		m_codeEditor->ShowMethods();
	}
	else if (event.GetId() == wxID_FORMAT_CODE) {
		m_codeEditor->FormatSelection();
	}
	else if (event.GetId() == wxID_INCREASE_INDENT) {
		m_codeEditor->IncreaseIndent();
	}
	else if (event.GetId() == wxID_DECREASE_INDENT) {
		m_codeEditor->DecreaseIndent();
	}
}

// ----------------------------------------------------------------------------
// ibModuleDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibModuleDocument, ibMetaDocument);

bool ibModuleDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

bool ibModuleDocument::OnOpenDocument(const wxString& filename)
{
	return ibMetaDocument::OnOpenDocument(filename);
}

bool ibModuleDocument::OnSaveDocument(const wxString& filename)
{
	return GetCodeEditor()->SaveModule();
}

bool ibModuleDocument::OnSaveModified()
{
	return ibMetaDocument::OnSaveModified();
}

bool ibModuleDocument::OnCloseDocument()
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr &&
		codeEditor->IsEditable()) {
		if (!codeEditor->SyntaxControl(false))
			return false;
	}
	return ibMetaDocument::OnCloseDocument();
}

wxCommandProcessor* ibModuleDocument::OnCreateCommandProcessor()
{
	ibModuleCommandProcessor* commandProcessor = new ibModuleCommandProcessor(GetCodeEditor());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibModuleDocument::DoSaveDocument(const wxString& filename)
{
	return GetCodeEditor()->SaveFile(filename);
}

bool ibModuleDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetCodeEditor()->LoadFile(filename))
		return false;

	return true;
}

bool ibModuleDocument::IsModified() const
{
	//wxStyledTextCtrl* wnd = GetCodeEditor();
	return ibMetaDocument::IsModified();// || (wnd && wnd->IsModified());
}

void ibModuleDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

bool ibModuleDocument::Save()
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr &&
		codeEditor->IsEditable()) {
		if (!codeEditor->SyntaxControl(false))
			return false;
	}
	return ibMetaDocument::Save();
}

// ----------------------------------------------------------------------------
// ibTextFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibModuleEditDocument, ibModuleDocument);

ibCodeEditor* ibModuleEditDocument::GetCodeEditor() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibModuleEditView)->GetCodeEditor() : nullptr;
}

void ibModuleEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	ibCodeEditor* autoComplete = GetCodeEditor();
	wxASSERT(autoComplete);
	autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
}

void ibModuleEditDocument::SetToolTip(const wxString& resultStr) {
	ibCodeEditor* codeEditor = GetCodeEditor();
	wxASSERT(codeEditor);
	if (codeEditor != nullptr) {
		codeEditor->SetToolTip(resultStr);
	}
}

void ibModuleEditDocument::ShowAutoComplete(const ibDebugAutoCompleteData& debugData)
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	wxASSERT(codeEditor);
	if (codeEditor != nullptr) {
		codeEditor->ShowAutoComplete(debugData);
	}
}
