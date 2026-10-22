#include "docViewFormEditor.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/picture.h"   // ibProtocolPicture — a class's picture, as the answer brought it

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
// ibFormEditView implementation
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibFormEditView, ibFrontendView)

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

bool ibFormEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	ibFormEditDocument* const doc = dynamic_cast<ibFormEditDocument*>(docBase);
	wxASSERT(doc);

	m_visualNotebook = new ibVisualEditorNotebook(doc, m_viewFrame, wxID_ANY, flags);
	m_visualNotebook->GetCodeEditor()->Bind(wxEVT_KILL_FOCUS, &ibFormEditView::OnKillFocus, this);

	// ⭐ THE FRAME THAT OPENS THE TAB CARRIES IT (State Opening) — the form, its controls' rows, the classes and the module's
	// text, as one fetch answers them; a server that put none there is asked, as before. A copy: the editor keeps it, and
	// the frame changes under it with the next answer (closing the tested form's tab took Opening away, and the form
	// tested again was sent as garbage — 2026-10-19).
	ibProtocolNode answer = doc->GetOpeningState().FindChild(ibProtocol::ibView::ibFormDesigner::Opening).Clone();
	if (!answer.IsNode() || !answer.Has(ibProtocol::ibView::ibFormDesigner::Form)) {
		answer = ibProtocolNode();
		if (!doc->Fetch(ibProtocolNode(), answer))
			return false;
	}

	// The palette — the classes a control may be made of, as the answer has them; the desktop's asked its registry
	// (a system control's class is not offered: Visible false).
	wxWindowID id = control_id;

	const std::vector<ibProtocolNode> classes = answer.FindChild(ibProtocol::ibView::ibFormDesigner::Classes).Children();
	std::vector<wxString> pictures;
	for (const ibProtocolNode& described : classes)
		pictures.push_back(described.GetString(ibProtocol::ibView::ibFormDesigner::Picture));
	ibProtocolPictureReceive(pictures);   // at once, not a call per class

	for (const ibProtocolNode& described : classes) {
		if (!described.GetBool(ibProtocol::ibView::ibFormDesigner::Visible, true))
			continue;
		const wxBitmap controlImage = ibProtocolPicture(described.GetString(ibProtocol::ibView::ibFormDesigner::Picture));
		if (controlImage.IsOk()) {
			m_controlDataArray.push_back({ described.GetString(ibProtocol::ibView::ibFormDesigner::Name), controlImage, id++ });
		}
	}

	Bind(wxEVT_MENU, &ibFormEditView::OnMenuEvent, this, control_id, id);

	if (!m_visualNotebook->LoadForm(answer))
		return false;

	m_visualNotebook->RefreshEditor();
	return ibFrontendView::OnCreate(docBase, flags);
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
			menuItem->Enable(m_visualNotebook->GetVisualEditor()->IsEditable());
			menuItem->SetBitmap(control.m_bmp);
		}

		menuForm->AppendSubMenu(menuControl, _("Controls"));

		mb->Append(menuForm, _("Form"));
		return mb;
	}

	return nullptr;
}
#endif

void ibFormEditView::OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView)
{
	if (activate) m_visualNotebook->ActivateEditor();
}

void ibFormEditView::OnUpdate(ibFrontendView* sender, wxObject* hint)
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
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — as the module's tab does (ibModuleEditView::OnClose).
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_visualNotebook->Freeze();

		m_visualNotebook->Destroy();
		m_visualNotebook = nullptr;

		return true;
	}

	return false;
}

#include "frmclient/artProvider/artProvider.h"

#include "frmclient/win/editor/textEditor/textEditorPrintOut.h"
#include "designer-thin/win/editor/visualEditor/printout/formPrintOut.h"

wxPrintout* ibFormEditView::OnCreatePrintout()
{
	if (m_visualNotebook->GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return new ibTextEditorPrintout(m_visualNotebook->GetCodeEditor(), m_viewDocument->GetTitle());
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
			toolbar->AddTool(wxID_INCREASE_INDENT, _("Increase indent"), wxArtProvider::GetBitmapBundle(wxART_INCREASE_INDENT, wxART_DOC_MODULE), _("Indent"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_INCREASE_INDENT, m_visualNotebook->IsEditable());
			toolbar->AddTool(wxID_DECREASE_INDENT, _("Decrease indent"), wxArtProvider::GetBitmapBundle(wxART_DECREASE_INDENT, wxART_DOC_MODULE), _("Unindent"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_DECREASE_INDENT, m_visualNotebook->IsEditable());
		}
	}
	else if (m_visualNotebook != nullptr) {
		for (auto control : m_controlDataArray) {
			toolbar->AddTool(control.m_id, control.m_name, control.m_bmp, control.m_name);
			toolbar->EnableTool(control.m_id, m_visualNotebook->GetVisualEditor()->IsEditable());
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
			return;
		}
		auto it = std::find_if(
			m_controlDataArray.begin(), m_controlDataArray.end(), [id](const ibControlData& ctrl) { return id == ctrl.m_id; });
		if (it != m_controlDataArray.end()) m_visualNotebook->CreateControl(it->m_name);
	}
}

void ibFormEditView::OnKillFocus(wxFocusEvent& event)
{
	// An editor torn down loses its focus too — what was typed there goes with it.
	ibCodeEditor* const codeEditor = m_visualNotebook != nullptr ? m_visualNotebook->GetCodeEditor() : nullptr;
	if (codeEditor != nullptr && !codeEditor->IsBeingDeleted())
		codeEditor->SaveModule();
	event.Skip();
}

// ----------------------------------------------------------------------------
// ibFormEditDocument implementation
// ----------------------------------------------------------------------------

void ibFormEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	// The designer's first — the module's drawing asks the menu and the toolbar anew when the read-only changed, and they
	// are built of both editors.
	ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
	if (visualNotebook != nullptr && (!patch.IsNode() || patch.Has(ibProtocol::ibView::State))) {
		const ibProtocolNode state = view.FindChild(ibProtocol::ibView::State);
		visualNotebook->GetVisualEditor()->SetReadOnly(state.GetBool(ibProtocol::ibView::ibModule::ReadOnly));
		visualNotebook->GetVisualEditor()->GetVisualEditor()->SetCaption(state.GetString(ibProtocol::ibView::ibFormDesigner::Caption));
	}

	ibModuleEditDocument::DrawView(view, patch);
}

wxCommandProcessor* ibFormEditDocument::OnCreateCommandProcessor()
{
	ibVisualDesignerCommandProcessor* commandProcessor = new ibVisualDesignerCommandProcessor(GetVisualNotebook());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

ibCodeEditor* ibFormEditDocument::GetCodeEditor() const
{
	ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
	if (visualNotebook != nullptr)
		return visualNotebook->GetCodeEditor();
	return nullptr;
}

void ibFormEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	if (lineBreakpoint > 0) {
		ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
			visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		}
	}

	ibModuleEditDocument::SetCurrentLine(lineBreakpoint, setBreakpoint);
}

ibVisualEditorNotebook* ibFormEditDocument::GetVisualNotebook() const
{
	ibFormEditView* view = dynamic_cast<ibFormEditView*>(GetFirstView());
	return view != nullptr ? view->GetVisualNotebook() : nullptr;
}

ibFrontendView* ibFormEditDocument::DoCreateView()
{
	return new ibFormEditView();
}
