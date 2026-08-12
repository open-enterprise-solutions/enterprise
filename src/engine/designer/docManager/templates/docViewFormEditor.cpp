#include "docViewFormEditor.h"
#include "frontend/mainFrame/mainFrame.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibFormEditView, ibMetaView);

enum {
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,
	wxID_SYNTAX_CONTROL,
	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS,
	wxID_FORMAT_CODE,
	wxID_INCREASE_INDENT,
	wxID_DECREASE_INDENT,

	wxID_TEST_FORM = 15001
};

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibFormEditView, ibMetaView)

EVT_MENU(wxID_COPY, ibFormEditView::OnCopy)
EVT_MENU(wxID_PASTE, ibFormEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, ibFormEditView::OnSelectAll)
EVT_FIND(wxID_ANY, ibFormEditView::OnFind)
EVT_FIND_NEXT(wxID_ANY, ibFormEditView::OnFind)

EVT_MENU(wxID_ADD_COMMENTS, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_REMOVE_COMMENTS, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_SYNTAX_CONTROL, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_GOTOLINE, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_PROCEDURES_FUNCTIONS, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_FORMAT_CODE, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_INCREASE_INDENT, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_DECREASE_INDENT, ibFormEditView::OnMenuEvent)
EVT_MENU(wxID_TEST_FORM, ibFormEditView::OnMenuEvent)

wxEND_EVENT_TABLE()

static wxWindowID control_id = wxID_HIGHEST + 3000;

bool ibFormEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_visualNotebook = new ibVisualEditorNotebook(doc, m_viewFrame, wxID_ANY, flags);

	wxWindowID id = control_id;

	for (auto controlClass : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
		const ibCtorControlTypeBase* objectSingle = dynamic_cast<const ibCtorControlTypeBase*>(controlClass);
		wxASSERT(objectSingle);
		if (!objectSingle->IsControlSystem()) {
			wxBitmap controlImage = objectSingle->GetClassIcon();
			if (controlImage.IsOk()) {
				m_controlDataArray.push_back({ controlClass->GetClassName(), controlImage, id++ });
			}
		}
	}

	Bind(wxEVT_MENU, &ibFormEditView::OnMenuEvent, this, control_id, id);

	if (!m_visualNotebook->LoadForm())
		return false;

	m_visualNotebook->RefreshEditor();
	return ibView::OnCreate(docBase, flags);
}

#if wxUSE_MENUS	
wxMenuBar* ibFormEditView::CreateMenuBar() const
{
	if (m_visualNotebook != nullptr && m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {

		wxMenuBar* mb = new wxMenuBar;

		// and its menu bar
		wxMenu* menuForm = new wxMenu;
		menuForm->Append(wxID_TEST_FORM, _("Test form\tCtrl+R"));
		menuForm->AppendSeparator();

		wxMenu* menuControl = new wxMenu;

		for (auto control : m_controlDataArray) {
			wxMenuItem* menuItem = menuControl->Append(control.m_id,
				control.m_name, control.m_name);
			menuItem->Enable(m_visualNotebook->IsEditable());
			menuItem->SetBitmap(control.m_bmp);
		}

		menuForm->AppendSubMenu(menuControl, _("Controls"));

		mb->Append(menuForm, _("Form"));
		return mb;
	}

	return nullptr;
}
#endif 

void ibFormEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_visualNotebook->ActivateEditor();
}

void ibFormEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_visualNotebook != nullptr)
		m_visualNotebook->RefreshEditor();
}

void ibFormEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibFormEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		m_visualNotebook->Freeze();

		m_visualNotebook->Destroy();
		m_visualNotebook = nullptr;

		return true;
	}

	return false;
}

#include "frontend/artProvider/artProvider.h"

#include "frontend/win/editor/codeEditor/codeEditorPrintOut.h"
#include "win/editor/visualEditor/printout/formPrintOut.h"

wxPrintout* ibFormEditView::OnCreatePrintout()
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return new ibCodeEditorPrintout(m_visualNotebook->GetCodeEditor(), m_viewDocument->GetTitle());
	return new ibFormPrintout(m_visualNotebook->GetVisualHost(), m_viewDocument->GetTitle());
}

void ibFormEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (m_visualNotebook != nullptr && m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		if (!toolbar->GetToolCount()) {
			toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxArtProvider::GetBitmapBundle(wxART_ADD_COMMENT, wxART_DOC_MODULE), _("Add"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_ADD_COMMENTS, m_visualNotebook->IsEditable());
			toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxArtProvider::GetBitmapBundle(wxART_REMOVE_COMMENT, wxART_DOC_MODULE), _("Remove"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_visualNotebook->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_SYNTAX_CONTROL, _("Syntax control"), wxArtProvider::GetBitmapBundle(wxART_SYNTAX_CONTROL, wxART_DOC_MODULE), _("Syntax"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_SYNTAX_CONTROL, m_visualNotebook->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxArtProvider::GetBitmapBundle(wxART_GOTO_LINE, wxART_DOC_MODULE), _("Goto"), wxItemKind::wxITEM_NORMAL);
			toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxArtProvider::GetBitmapBundle(wxART_PROC_AND_FUNC, wxART_DOC_MODULE), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_FORMAT_CODE, _("Format selection"), wxArtProvider::GetBitmapBundle(wxART_FORMAT_CODE, wxART_DOC_MODULE), _("Format"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_FORMAT_CODE, m_visualNotebook->IsEditable());
			toolbar->AddTool(wxID_INCREASE_INDENT, _("Increase indent"), wxArtProvider::GetBitmapBundle(wxART_GO_FORWARD, wxART_TOOLBAR, wxSize(16, 16)), _("Indent"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_INCREASE_INDENT, m_visualNotebook->IsEditable());
			toolbar->AddTool(wxID_DECREASE_INDENT, _("Decrease indent"), wxArtProvider::GetBitmapBundle(wxART_GO_BACK, wxART_TOOLBAR, wxSize(16, 16)), _("Unindent"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_DECREASE_INDENT, m_visualNotebook->IsEditable());
		}
	}
	else if (m_visualNotebook != nullptr) {
		for (auto control : m_controlDataArray) {
			toolbar->AddTool(control.m_id, control.m_name, control.m_bmp, control.m_name);
			toolbar->EnableTool(control.m_id, m_visualNotebook->IsEditable());
		}
	}
}

void ibFormEditView::OnMenuEvent(wxCommandEvent& event)
{
	wxWindowID id = event.GetId();

	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		ibCodeEditor* codeEditor = m_visualNotebook->GetCodeEditor();
		if (id == wxID_ADD_COMMENTS) {
			int nStartLine, nEndLine;
			codeEditor->GetSelection(&nStartLine, &nEndLine);
			for (int line = codeEditor->LineFromPosition(nStartLine); line <= codeEditor->LineFromPosition(nEndLine); line++) {
				codeEditor->Replace(codeEditor->PositionFromLine(line), codeEditor->PositionFromLine(line), "//");
			}
		}
		else if (id == wxID_REMOVE_COMMENTS) {
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
		else if (id == wxID_SYNTAX_CONTROL) {
			m_visualNotebook->SyntaxControl();
		}
		else if (id == wxID_GOTOLINE) {
			m_visualNotebook->ShowGotoLine();
		}
		else if (id == wxID_PROCEDURES_FUNCTIONS) {
			m_visualNotebook->ShowMethods();
		}
		else if (id == wxID_FORMAT_CODE) {
			codeEditor->FormatSelection();
		}
		else if (id == wxID_INCREASE_INDENT) {
			codeEditor->IncreaseIndent();
		}
		else if (id == wxID_DECREASE_INDENT) {
			codeEditor->DecreaseIndent();
		}
	}
	else if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {

		if (id == wxID_TEST_FORM) {
			m_visualNotebook->TestForm();
		}
		else {
			auto it = std::find_if(
				m_controlDataArray.begin(), m_controlDataArray.end(), [id](ibControlData& ctrl) { return id == ctrl.m_id; });
			if (it != m_controlDataArray.end()) m_visualNotebook->CreateControl(it->m_name);
		}
	}
}

// ----------------------------------------------------------------------------
// ibModuleDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibFormDocument, ibValueModuleDocument);

bool ibFormDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

bool ibFormDocument::OnOpenDocument(const wxString& filename)
{
	return ibMetaDocument::OnOpenDocument(filename);
}

bool ibFormDocument::OnSaveDocument(const wxString& filename)
{
	return GetVisualNotebook()->SaveForm();
}

bool ibFormDocument::OnSaveModified()
{
	return ibMetaDocument::OnSaveModified();
}

bool ibFormDocument::OnCloseDocument()
{
	ibVisualEditorNotebook* visualNotebook = GetVisualNotebook();
	if (visualNotebook != nullptr && visualNotebook->IsEditable()) {
		if (!visualNotebook->SyntaxControl(false))
			return false;
	}

	return ibMetaDocument::OnCloseDocument();
}

wxCommandProcessor* ibFormDocument::OnCreateCommandProcessor()
{
	ibVisualDesignerCommandProcessor* commandProcessor = new ibVisualDesignerCommandProcessor(GetVisualNotebook());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

bool ibFormDocument::IsModified() const
{
	//wxStyledTextCtrl* wnd = GetCodeEditor();
	return ibMetaDocument::IsModified();// || (wnd && wnd->IsModified());
}

void ibFormDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

bool ibFormDocument::Save()
{
	ibVisualEditorNotebook* visualNotebook = GetVisualNotebook();
	if (visualNotebook != nullptr && visualNotebook->IsEditable()) {
		if (!visualNotebook->SyntaxControl(false))
			return false;
	}
	return ibMetaDocument::Save();
}

// ----------------------------------------------------------------------------
// ibTextFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFormEditDocument, ibFormDocument);

ibVisualEditorNotebook* ibFormEditDocument::GetVisualNotebook() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibFormEditView)->GetVisualNotebook() : nullptr;
}