#include "module.h"
#include "moduleCMD.h"
#include "frontend/mainFrame.h"
#include "metadata/metaObjectsDefines.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CModuleView, CView);

wxBEGIN_EVENT_TABLE(CModuleView, CView)
EVT_MENU(wxID_COPY, CModuleView::OnCopy)
EVT_MENU(wxID_PASTE, CModuleView::OnPaste)
EVT_MENU(wxID_SELECTALL, CModuleView::OnSelectAll)
EVT_FIND(wxID_ANY, CModuleView::OnFind)
EVT_FIND_NEXT(wxID_ANY, CModuleView::OnFind)
wxEND_EVENT_TABLE()

bool CModuleView::OnCreate(CDocument *doc, long flags)
{
	m_code = new CCodeEditorCtrl(doc, m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_code->SetEditorSettings(mainFrame->GetEditorSettings());
	m_code->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_code->SetReadOnly(flags == wxDOC_READONLY);
	m_code->SetSTCFocus(true);

	return CView::OnCreate(doc, flags);
}

#include <wx/artprov.h>

enum
{
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,

	wxID_SYSNTAX_CONTROL,

	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS
};

#include "core/resources/codeEditor/addComment.xpm"
#include "core/resources/codeEditor/removeComment.xpm"
#include "core/resources/codeEditor/syntaxControl.xpm"
#include "core/resources/codeEditor/gotoLine.xpm"
#include "core/resources/codeEditor/proceduresFunctions.xpm"

void CModuleView::OnCreateToolbar(wxAuiToolBar *toolbar)
{
	if (m_code == NULL)
		return;

	if (!toolbar->GetToolCount()) {
		toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxIcon(s_addComment_xpm), _("Add"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_ADD_COMMENTS, m_code->IsEditable());
		toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxIcon(s_removeComment_xpm), _("Remove"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_code->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_SYSNTAX_CONTROL, _("Syntax control"), wxIcon(s_syntaxControl_xpm), _("Syntax"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_SYSNTAX_CONTROL, m_code->IsEditable());
		toolbar->AddSeparator();
		toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxIcon(s_gotoLine_xpm), _("Goto"), wxItemKind::wxITEM_NORMAL);
		toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxIcon(s_proceduresFunctions_xpm), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
	}

	toolbar->Bind(wxEVT_MENU, &CModuleView::OnMenuClicked, this);
}

void CModuleView::OnRemoveToolbar(wxAuiToolBar *toolbar)
{
	toolbar->Unbind(wxEVT_MENU, &CModuleView::OnMenuClicked, this);
}

void CModuleView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

void CModuleView::OnUpdate(wxView *sender, wxObject *hint)
{
	m_code->SetEditorSettings(mainFrame->GetEditorSettings());
	m_code->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_code->UpdateBreakpoints();
}

bool CModuleView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

#include "frontend/codeEditor/codeEditorCtrlPrintOut.h"

wxPrintout *CModuleView::OnCreatePrintout()
{
	return new CCodeEditorPrintout(m_code, this->GetViewName());
}

#include "metadata/metadata.h"
#include "compiler/systemObjects.h"

void CModuleView::OnFind(wxFindDialogEvent& event)
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
		m_code->SetSelectionStart(m_code->GetSelectionEnd());
		m_code->SearchAnchor();
		result = m_code->SearchNext(sciflags, event.GetFindString());
	}
	else
	{
		m_code->SetSelectionEnd(m_code->GetSelectionStart());
		m_code->SearchAnchor();
		result = m_code->SearchPrev(sciflags, event.GetFindString());
	}
	if (wxSTC_INVALID_POSITION == result)
	{
		wxMessageBox(wxString::Format(_("\"%s\" not found!"), event.GetFindString().c_str()), _("Not Found!"), wxICON_ERROR, (wxWindow*)event.GetClientData());
	}
	else
	{
		m_code->EnsureCaretVisible();
	}
}

#include "frontend/windows/lineInputWnd.h"
#include "frontend/windows/functionlist/functionlistWnd.h"

void CModuleView::OnMenuClicked(wxCommandEvent& event)
{
	CDocument *docView = dynamic_cast<CDocument *>(m_viewDocument);

	if (event.GetId() == wxID_ADD_COMMENTS) {
		int nStartLine, nEndLine;
		m_code->GetSelection(&nStartLine, &nEndLine);
		for (int line = m_code->LineFromPosition(nStartLine); line <= m_code->LineFromPosition(nEndLine); line++) {
			m_code->Replace(m_code->PositionFromLine(line), m_code->PositionFromLine(line), "//");
		}
	}
	else if (event.GetId() == wxID_REMOVE_COMMENTS) {
		int nStartLine, nEndLine;
		m_code->GetSelection(&nStartLine, &nEndLine);
		for (int line = m_code->LineFromPosition(nStartLine); line <= m_code->LineFromPosition(nEndLine); line++) {
			int startPos = m_code->PositionFromLine(line);
			wxString sLine = m_code->GetLineRaw(line);
			for (unsigned int i = 0; i < sLine.length(); i++) {
				if (sLine[i] == '/'
					&& (i + 1 < sLine.length() && sLine[i + 1] == '/')) {
					m_code->Replace(startPos + i, startPos + i + 2, wxEmptyString); break;
				}
			}
		}
	}
	else if (event.GetId() == wxID_SYSNTAX_CONTROL) {
		IMetaObject *metaObject = docView->GetMetaObject();
		wxASSERT(metaObject);
		IMetadata *metaData = metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager *moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IModuleInfo *dataRef = NULL;

		if (moduleManager->FindCompileModule(metaObject, dataRef)) {
			CCompileModule *m_compileModule = dataRef->GetCompileModule();
			try
			{
				if (m_compileModule->Compile()) {
					CSystemObjects::Message(_("No syntax errors detected!"));
				}
				else {
					wxASSERT("CCompileModule::Compile return false");
				}
			}
			catch (...)
			{
			}
		}
	}
	else if (event.GetId() == wxID_GOTOLINE) {
		CLineInput *lineInput = new CLineInput(m_code);
		int ret = lineInput->ShowModal();
		if (ret != wxNOT_FOUND) {
			m_code->SetFocus();
			m_code->GotoLine(ret - 1);
		}
	}
	else if (event.GetId() == wxID_PROCEDURES_FUNCTIONS) {
		CFunctionList *funcList = new CFunctionList(GetDocument(), m_code);
		int ret = funcList->ShowModal();
	}

	//event.Skip();
}

// ----------------------------------------------------------------------------
// CModuleDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CModuleDocument, CDocument);

CCommandProcessor *CModuleDocument::CreateCommandProcessor()
{
	CCodeEditorCtrl* m_context = GetTextCtrl();
	CModuleCommandProcessor *commandProcessor = new CModuleCommandProcessor(m_context);

	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();

	return commandProcessor;
}

bool CModuleDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return GetTextCtrl()->LoadModule();
}

bool CModuleDocument::OnOpenDocument(const wxString& filename)
{
	return CDocument::OnOpenDocument(filename);
}

bool CModuleDocument::OnSaveDocument(const wxString& filename)
{
	return GetTextCtrl()->SaveModule();
}

bool CModuleDocument::OnSaveModified()
{
	return CDocument::OnSaveModified();
}

bool CModuleDocument::OnCloseDocument()
{
	CCodeEditorCtrl *autocomplete = GetTextCtrl();

	if (autocomplete &&
		autocomplete->IsEditable()) {
		wxASSERT(m_metaObject);
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager *moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IModuleInfo *dataRef = NULL;

		if (moduleManager->FindCompileModule(m_metaObject, dataRef))
		{
			CCompileModule *compileModule = dataRef->GetCompileModule();
			try
			{
				if (!compileModule->Compile()) {
					wxASSERT("CCompileModule::Compile return false");
				}
			}
			catch (...)
			{
				int answer = wxMessageBox(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	return CDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CModuleDocument::DoSaveDocument(const wxString& filename)
{
	return GetTextCtrl()->SaveFile(filename);
}

bool CModuleDocument::DoOpenDocument(const wxString& filename)
{
	if (!GetTextCtrl()->LoadFile(filename))
		return false;

	return true;
}

bool CModuleDocument::IsModified() const
{
	//wxStyledTextCtrl* wnd = GetTextCtrl();
	return CDocument::IsModified();// || (wnd && wnd->IsModified());
}

void CModuleDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

bool CModuleDocument::Save()
{
	CCodeEditorCtrl *autocomplete = GetTextCtrl();

	if (autocomplete &&
		autocomplete->IsEditable()) {
		wxASSERT(m_metaObject);
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager *moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IModuleInfo *dataRef = NULL;

		if (moduleManager->FindCompileModule(m_metaObject, dataRef))
		{
			CCompileModule *compileModule = dataRef->GetCompileModule();

			try
			{
				if (!compileModule->Compile()) {
					wxASSERT("CCompileModule::Compile return false");
				}
			}
			catch (...)
			{
				int answer = wxMessageBox(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	return CDocument::Save();
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CModuleEditDocument, CModuleDocument);

CCodeEditorCtrl* CModuleEditDocument::GetTextCtrl() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CModuleView)->GetText() : NULL;
}

void CModuleEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	CCodeEditorCtrl *autoComplete = GetTextCtrl();
	wxASSERT(autoComplete);
	autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
}