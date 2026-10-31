#include "docViewModuleEditor.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "designer-thin/win/editor/codeEditor/codeEditorDesigner.h"

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
// ibModuleEditView implementation
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibModuleEditView, ibFrontendView)
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

bool ibModuleEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	ibModuleEditDocument* const doc = dynamic_cast<ibModuleEditDocument*>(docBase);
	wxASSERT(doc);

	// The desktop's designer editor — the debugger's margin, its patches, its values (ibCodeEditorDesigner).
	m_codeEditor = new ibCodeEditorDesigner(doc, m_viewFrame, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_codeEditor->SetReadOnly(flags == ibDOC_READONLY);
	m_codeEditor->SetSTCFocus(true);
	m_codeEditor->Bind(wxEVT_KILL_FOCUS, &ibModuleEditView::OnKillFocus, this);

	// ⭐ THE FRAME THAT OPENS THE TAB CARRIES ITS TEXT (State Text), as the form editor's carries its form (Opening); a
	// server that put none there is fetched, as before.
	const ibProtocolNode& state = doc->GetOpeningState();
	return ibFrontendView::OnCreate(docBase, flags)
		&& (state.Has(ibProtocol::ibView::ibModule::Text) ? m_codeEditor->LoadModule(state) : m_codeEditor->LoadModule());
}

void ibModuleEditView::OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView)
{
	if (activate) m_codeEditor->ActivateEditor();
}

void ibModuleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

void ibModuleEditView::OnUpdate(ibFrontendView* sender, wxObject* hint)
{
	if (m_codeEditor != nullptr)
		m_codeEditor->RefreshEditor();
}

bool ibModuleEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did (ibAuditLogView::OnClose).
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_codeEditor->Freeze();

		m_codeEditor->Destroy();
		m_codeEditor = nullptr;

		return true;
	}

	return false;
}

#include "frmclient/win/editor/textEditor/textEditorPrintOut.h"

wxPrintout* ibModuleEditView::OnCreatePrintout()
{
	return new ibTextEditorPrintout(m_codeEditor, m_viewDocument->GetTitle());
}

#include "frmclient/artProvider/artProvider.h"

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
		toolbar->AddTool(wxID_INCREASE_INDENT, _("Increase indent"), wxArtProvider::GetBitmapBundle(wxART_INCREASE_INDENT, wxART_DOC_MODULE), _("Indent"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_INCREASE_INDENT, m_codeEditor->IsEditable());
		toolbar->AddTool(wxID_DECREASE_INDENT, _("Decrease indent"), wxArtProvider::GetBitmapBundle(wxART_DECREASE_INDENT, wxART_DOC_MODULE), _("Unindent"), wxItemKind::wxITEM_NORMAL);
		toolbar->EnableTool(wxID_DECREASE_INDENT, m_codeEditor->IsEditable());
	}
}

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

void ibModuleEditView::OnKillFocus(wxFocusEvent& event)
{
	// An editor torn down loses its focus too — what was typed there goes with it.
	if (m_codeEditor != nullptr && !m_codeEditor->IsBeingDeleted())
		m_codeEditor->SaveModule();
	event.Skip();
}

// ----------------------------------------------------------------------------
// ibModuleEditDocument implementation
// ----------------------------------------------------------------------------

void ibModuleEditDocument::Modify(bool modify)
{
	if (modify && !m_modified) {
		m_modified = true;
		Post(ibProtocol::ibEvent::ibKind::Input);
	}
}

bool ibModuleEditDocument::Close()
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr)
		codeEditor->SaveModule();
	return ibTabDocument::Close();
}

bool ibModuleEditDocument::Save()
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr)
		codeEditor->SaveModule();
	return ibTabDocument::Save();
}

void ibModuleEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocol::ibView::State))
		return;

	const ibProtocolNode state = view.FindChild(ibProtocol::ibView::State);
	m_modified = state.GetBool(ibProtocol::ibView::ibModule::Modified);

	const wxString docPath = state.GetString(ibProtocol::ibView::ibModule::Module);
	const bool docPathChanged = docPath != m_docPath;
	m_docPath = docPath;

	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor == nullptr)
		return;

	// The syntax the module is written in, and whether it takes typing — the desktop's editor asked the process and
	// the document (ibCompileCode::GetCodeStyle, ibDOC_READONLY).
	codeEditor->SetCodeStyle(static_cast<short>(state.GetInt(ibProtocol::ibView::ibModule::Syntax)));
	const bool readOnly = state.GetBool(ibProtocol::ibView::ibModule::ReadOnly);
	const bool readOnlyChanged = readOnly == codeEditor->IsEditable();
	codeEditor->SetReadOnly(readOnly);
	// …the menu and the toolbar asked anew when that changed: built as the tab was activated, before the server said —
	// the desktop's were built of a document opened read-only already.
	if (readOnlyChanged)
		Activate();

	// Its breakpoints, drawn once the address they are kept by is known.
	if (docPathChanged)
		codeEditor->RefreshBreakpoint();
}

void ibModuleEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr)
		codeEditor->SetCurrentLine(lineBreakpoint, setBreakpoint);
}

void ibModuleEditDocument::SetToolTip(const wxString& resultStr)
{
	ibCodeEditor* codeEditor = GetCodeEditor();
	if (codeEditor != nullptr) {
		// The editor's own door, not the window's: it keeps the answer as well as showing it.
		codeEditor->SetDebugValue(resultStr);
	}
}

wxCommandProcessor* ibModuleEditDocument::OnCreateCommandProcessor()
{
	ibModuleCommandProcessor* commandProcessor = new ibModuleCommandProcessor(GetCodeEditor());
	commandProcessor->SetEditMenu(mainFrame->GetDefaultMenu(wxID_EDIT));
	commandProcessor->Initialize();
	return commandProcessor;
}

ibCodeEditor* ibModuleEditDocument::GetCodeEditor() const
{
	ibModuleEditView* view = dynamic_cast<ibModuleEditView*>(GetFirstView());
	return view != nullptr ? view->GetCodeEditor() : nullptr;
}

ibFrontendView* ibModuleEditDocument::DoCreateView()
{
	return new ibModuleEditView();
}
