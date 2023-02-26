#include "moduleEditor.h"

#include "frontend/mainFrame.h"
#include "core/metadata/metaObjectsDefines.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CModuleEditView, CView);

wxBEGIN_EVENT_TABLE(CModuleEditView, CView)
EVT_MENU(wxID_COPY, CModuleEditView::OnCopy)
EVT_MENU(wxID_PASTE, CModuleEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CModuleEditView::OnSelectAll)
EVT_FIND(wxID_ANY, CModuleEditView::OnFind)
EVT_FIND_NEXT(wxID_ANY, CModuleEditView::OnFind)
wxEND_EVENT_TABLE()

bool CModuleEditView::OnCreate(CDocument* doc, long flags)
{
	m_codeEditor = new CCodeEditorCtrl(doc, m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_codeEditor->SetReadOnly(flags == wxDOC_READONLY);
	m_codeEditor->SetSTCFocus(true);

	return CView::OnCreate(doc, flags);
}

enum
{
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,

	wxID_SYNTAX_CONTROL,

	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS
};

#include "core/art/artProvider.h"

void CModuleEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (m_codeEditor == NULL)
		return;

	if (!toolbar->GetToolCount()) {
		toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxArtProvider::GetBitmap(wxART_ADD_COMMENT, wxART_DOC_MODULE), _("Add"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_ADD_COMMENTS, m_codeEditor->IsEditable());
		toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxArtProvider::GetBitmap(wxART_REMOVE_COMMENT, wxART_DOC_MODULE), _("Remove"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_codeEditor->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_SYNTAX_CONTROL, _("Syntax control"), wxArtProvider::GetBitmap(wxART_SYNTAX_CONTROL, wxART_DOC_MODULE), _("Syntax"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_SYNTAX_CONTROL, m_codeEditor->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxArtProvider::GetBitmap(wxART_GOTO_LINE, wxART_DOC_MODULE), _("Goto"), wxItemKind::wxITEM_NORMAL);
		toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxArtProvider::GetBitmap(wxART_PROC_AND_FUNC, wxART_DOC_MODULE), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
	}

	toolbar->Bind(wxEVT_MENU, &CModuleEditView::OnMenuClicked, this);
}

void CModuleEditView::OnRemoveToolbar(wxAuiToolBar* toolbar)
{
	toolbar->Unbind(wxEVT_MENU, &CModuleEditView::OnMenuClicked, this);
}

void CModuleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

void CModuleEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_codeEditor != NULL)
		m_codeEditor->RefreshEditor();
}

bool CModuleEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	if (CView::OnClose(deleteWindow))
		return m_codeEditor->Destroy();

	return false;
}

#include "frontend/codeEditor/codeEditorCtrlPrintOut.h"

wxPrintout* CModuleEditView::OnCreatePrintout()
{
	return new CCodeEditorPrintout(m_codeEditor, this->GetViewName());
}

#include "frontend/windows/lineInputWnd.h"
#include "frontend/windows/functionlist/functionlistWnd.h"

void CModuleEditView::OnMenuClicked(wxCommandEvent& event)
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
}

// ----------------------------------------------------------------------------
// CModuleDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CModuleDocument, CDocument);

wxCommandProcessor* CModuleDocument::CreateCommandProcessor() const
{
	CModuleCommandProcessor* commandProcessor = new CModuleCommandProcessor(GetCodeEditor());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

bool CModuleDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return GetCodeEditor()->LoadModule();
}

bool CModuleDocument::OnOpenDocument(const wxString& filename)
{
	return CDocument::OnOpenDocument(filename);
}

bool CModuleDocument::OnSaveDocument(const wxString& filename)
{
	return GetCodeEditor()->SaveModule();
}

bool CModuleDocument::OnSaveModified()
{
	return CDocument::OnSaveModified();
}

bool CModuleDocument::OnCloseDocument()
{
	CCodeEditorCtrl* codeEditor = GetCodeEditor();
	if (codeEditor != NULL &&
		codeEditor->IsEditable()) {
		if (!codeEditor->SyntaxControl(false))
			return false;
	}
	return CDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CModuleDocument::DoSaveDocument(const wxString& filename)
{
	return GetCodeEditor()->SaveFile(filename);
}

bool CModuleDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetCodeEditor()->LoadFile(filename))
		return false;

	return true;
}

bool CModuleDocument::IsModified() const
{
	//wxStyledTextCtrl* wnd = GetCodeEditor();
	return CDocument::IsModified();// || (wnd && wnd->IsModified());
}

void CModuleDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

bool CModuleDocument::Save()
{
	CCodeEditorCtrl* codeEditor = GetCodeEditor();
	if (codeEditor != NULL &&
		codeEditor->IsEditable()) {
		if (!codeEditor->SyntaxControl(false))
			return false;
	}
	return CDocument::Save();
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CModuleEditDocument, CModuleDocument);

CCodeEditorCtrl* CModuleEditDocument::GetCodeEditor() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CModuleEditView)->GetCodeEditor() : NULL;
}

void CModuleEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	CCodeEditorCtrl* autoComplete = GetCodeEditor();
	wxASSERT(autoComplete);
	autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
}