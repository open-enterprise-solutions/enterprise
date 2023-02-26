#include "formEditor.h"
#include "frontend/mainFrame.h"

wxIMPLEMENT_DYNAMIC_CLASS(CFormEditView, CView);

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(CFormEditView, CView)
EVT_MENU(wxID_COPY, CFormEditView::OnCopy)
EVT_MENU(wxID_PASTE, CFormEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CFormEditView::OnSelectAll)
EVT_FIND(wxID_ANY, CFormEditView::OnFind)
EVT_FIND_NEXT(wxID_ANY, CFormEditView::OnFind)
wxEND_EVENT_TABLE()

#define wxID_TEST_FORM 15001

static wxWindowID nextId = wxID_HIGHEST + 3000;

#include "core/art/artProvider.h"

bool CFormEditView::OnCreate(CDocument* doc, long flags)
{
	m_visualNotebook = new CVisualEditorNotebook(doc, m_viewFrame, wxID_ANY, flags);
	return CView::OnCreate(doc, flags);
}

enum {
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,
	wxID_SYNTAX_CONTROL,
	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS
};

#include "frontend/visualView/visualEditor.h"

void CFormEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		if (!toolbar->GetToolCount()) {
			toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxArtProvider::GetBitmap(wxART_ADD_COMMENT, wxART_DOC_MODULE), _("Add"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_ADD_COMMENTS, m_visualNotebook->IsEditable());
			toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxArtProvider::GetBitmap(wxART_REMOVE_COMMENT, wxART_DOC_MODULE), _("Remove"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_visualNotebook->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_SYNTAX_CONTROL, _("Syntax control"), wxArtProvider::GetBitmap(wxART_SYNTAX_CONTROL, wxART_DOC_MODULE), _("Syntax"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_SYNTAX_CONTROL, m_visualNotebook->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxArtProvider::GetBitmap(wxART_GOTO_LINE, wxART_DOC_MODULE), _("Goto"), wxItemKind::wxITEM_NORMAL);
			toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxArtProvider::GetBitmap(wxART_PROC_AND_FUNC, wxART_DOC_MODULE), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
		}
	}
	else {
		for (auto controlClass : CValue::GetAvailableObjects(eObjectType::eObjectType_object_control)) {
			IControlValueAbstract* objectSingle = dynamic_cast<IControlValueAbstract*>(
				CValue::GetAvailableObject(controlClass)
				);
			wxASSERT(objectSingle);
			if (!objectSingle->IsControlSystem()) {
				const wxBitmap& controlImage = objectSingle->GetClassIcon();
				if (controlImage.IsOk()) {
					toolbar->AddTool(nextId++, controlClass, controlImage, controlClass);
					toolbar->EnableTool(nextId - 1, m_visualNotebook->IsEditable());
				}
			}
		}
	}

	toolbar->Bind(wxEVT_MENU, &CFormEditView::OnMenuClicked, this);
}

void CFormEditView::OnRemoveToolbar(wxAuiToolBar* toolbar)
{
	toolbar->Unbind(wxEVT_MENU, &CFormEditView::OnMenuClicked, this);
}

#if wxUSE_MENUS	

wxMenu* CFormEditView::CreateViewMenu() const
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
		// and its menu bar
		wxMenu* menuForm = new wxMenu(_("Form"));
		menuForm->Append(wxID_TEST_FORM, _("Test form"));
		return menuForm;
	}
	return NULL;
}

void CFormEditView::OnMenuItemClicked(int id)
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
		if (id == wxID_TEST_FORM)
			m_visualNotebook->TestForm();
	}
}

#endif 

void CFormEditView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	CView::OnActivateView(activate, activeView, deactiveView);
}

void CFormEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_visualNotebook != NULL)
		m_visualNotebook->RefreshEditor();
}

void CFormEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CFormEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	if (CView::OnClose(deleteWindow))
		return m_visualNotebook->Destroy();

	return false;
}

#include "frontend/codeEditor/codeEditorCtrlPrintOut.h"
#include "frontend/visualView/printout/formPrintOut.h"

wxPrintout* CFormEditView::OnCreatePrintout()
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		return new CCodeEditorPrintout(
			m_visualNotebook->GetCodeEditor(), this->GetViewName()
		);
	}

	return new CFormPrintout(m_visualNotebook->GetVisualHost());
}

void CFormEditView::OnMenuClicked(wxCommandEvent& event)
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		CCodeEditorCtrl* codeEditor = m_visualNotebook->GetCodeEditor();
		if (event.GetId() == wxID_ADD_COMMENTS) {
			int nStartLine, nEndLine;
			codeEditor->GetSelection(&nStartLine, &nEndLine);
			for (int line = codeEditor->LineFromPosition(nStartLine); line <= codeEditor->LineFromPosition(nEndLine); line++) {
				codeEditor->Replace(codeEditor->PositionFromLine(line), codeEditor->PositionFromLine(line), "//");
			}
		}
		else if (event.GetId() == wxID_REMOVE_COMMENTS) {
			int nStartLine, nEndLine;
			codeEditor->GetSelection(&nStartLine, &nEndLine);
			for (int line = codeEditor->LineFromPosition(nStartLine); line <= codeEditor->LineFromPosition(nEndLine); line++) {
				int startPos = codeEditor->PositionFromLine(line);
				wxString sLine = codeEditor->GetLineRaw(line);
				for (unsigned int i = 0; i < sLine.length(); i++) {
					if (sLine[i] == '/'
						&& (i + 1 < sLine.length() && sLine[i + 1] == '/')) {
						codeEditor->Replace(startPos + i, startPos + i + 2, wxEmptyString); break;
					}
				}
			}
		}
		else if (event.GetId() == wxID_SYNTAX_CONTROL) {
			m_visualNotebook->SyntaxControl();
		}
		else if (event.GetId() == wxID_GOTOLINE) {
			m_visualNotebook->ShowGotoLine();
		}
		else if (event.GetId() == wxID_PROCEDURES_FUNCTIONS) {
			m_visualNotebook->ShowMethods();
		}
	}
	else {
		wxAuiToolBar* toolBar = mainFrame->GetDocToolbar();
		m_visualNotebook->CreateControl(
			toolBar->GetToolShortHelp(event.GetId())
		);
	}
}

// ----------------------------------------------------------------------------
// CModuleDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CFormDocument, CDocument);

wxCommandProcessor* CFormDocument::CreateCommandProcessor() const
{
	CVisualDesignerCommandProcessor* commandProcessor = new CVisualDesignerCommandProcessor(GetVisualNotebook());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

bool CFormDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return GetVisualNotebook()->LoadForm();
}

bool CFormDocument::OnOpenDocument(const wxString& filename)
{
	return CDocument::OnOpenDocument(filename);
}

bool CFormDocument::OnSaveDocument(const wxString& filename)
{
	return GetVisualNotebook()->SaveForm();
}

bool CFormDocument::OnSaveModified()
{
	return CDocument::OnSaveModified();
}

bool CFormDocument::OnCloseDocument()
{
	CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
	if (visualNotebook != NULL && visualNotebook->IsEditable()) {
		if (!visualNotebook->SyntaxControl(false))
			return false;
	}

	objectInspector->SelectObject((IPropertyObject*)m_metaObject);
	return CDocument::OnCloseDocument();
}

bool CFormDocument::IsModified() const
{
	//wxStyledTextCtrl* wnd = GetCodeEditor();
	return CDocument::IsModified();// || (wnd && wnd->IsModified());
}

void CFormDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

bool CFormDocument::Save()
{
	CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
	if (visualNotebook != NULL && visualNotebook->IsEditable()) {
		if (!visualNotebook->SyntaxControl(false))
			return false;
	}
	return CDocument::Save();
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CFormEditDocument, CFormDocument);

CVisualEditorNotebook* CFormEditDocument::GetVisualNotebook() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CFormEditView)->GetVisualNotebook() : NULL;
}
