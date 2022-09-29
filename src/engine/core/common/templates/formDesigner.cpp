#include "formDesigner.h"
#include "compiler/systemObjects.h"
#include "common/propertyObject.h"
#include "utils/typeconv.h"
#include "appData.h"
#include "frontend/mainFrame.h"
#include "frontend/visualView/visualEditor.h"
#include "metadata/metaObjects/metaObject.h"
#include "metadata/metadata.h"
#include "frontend/windows/lineInputWnd.h"
#include "frontend/windows/functionlist/functionlistWnd.h"
#include "frontend/objinspect/objinspect.h"

wxIMPLEMENT_DYNAMIC_CLASS(CFormEditView, CView);

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(CFormEditView, CView)
EVT_MENU(wxID_COPY, CFormEditView::OnCopy)
EVT_MENU(wxID_PASTE, CFormEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CFormEditView::OnSelectAll)
EVT_CODE_GENERATION(CFormEditView::OnEventHandlerModified)
EVT_FIND(wxID_ANY, CFormEditView::OnFind)
EVT_FIND_NEXT(wxID_ANY, CFormEditView::OnFind)
wxEND_EVENT_TABLE()

#define wxID_TEST_FORM 15001

static wxWindowID nextId = wxID_HIGHEST + 3000;

wxMenu* CreateMenuForm()
{
	// and its menu bar
	wxMenu* menuForm = new wxMenu();
	menuForm->Append(wxID_TEST_FORM, _("Test form"));
	return menuForm;
}

#include "frontend/theme/luna_auitabart.h"

#include "core/resources/designerPage.xpm"
#include "core/resources/codePage.xpm"

#define formName _("Forms")
#define formPos 2

bool CFormEditView::OnCreate(CDocument* doc, long flags)
{
	m_notebook = new wxAuiNotebook(m_viewFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_BOTTOM | wxAUI_NB_TAB_FIXED_WIDTH);
	m_notebook->SetArtProvider(new CLunaTabArt());
	m_notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &CFormEditView::OnPageChanged, this);

	InitializeFormDesigner(flags);
	InitializeCodeView(flags);

	m_notebook->AddPage(m_visualEditor, _("Designer"), false, wxIcon(s_designerPage_xpm));
	m_notebook->AddPage(m_codeEditor, _("Code"), false, wxIcon(s_codePage_xpm));

	if (mainFrame->GetMenuBar()->FindMenu(formName) == wxNOT_FOUND) {
		mainFrame->Connect(wxEVT_MENU, wxCommandEventHandler(CFormEditView::OnMenuClick), NULL, this);
		mainFrame->GetMenuBar()->Insert(formPos, CreateMenuForm(), formName);
	}

	m_visualEditor->ActivateObject();
	return CView::OnCreate(doc, flags);
}

enum
{
	wxID_ADD_COMMENTS = wxID_HIGHEST + 10000,
	wxID_REMOVE_COMMENTS,

	wxID_SYSNTAX_CONTROL,

	wxID_GOTOLINE,
	wxID_PROCEDURES_FUNCTIONS

};

#include "frontend/visualView/visualEditor.h"

#include "core/resources/codeEditor/addComment.xpm"
#include "core/resources/codeEditor/removeComment.xpm"
#include "core/resources/codeEditor/syntaxControl.xpm"
#include "core/resources/codeEditor/gotoLine.xpm"
#include "core/resources/codeEditor/proceduresFunctions.xpm"

void CFormEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (IsEditorActivate()) {
		if (!toolbar->GetToolCount()) {
			toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), wxIcon(s_addComment_xpm), _("Add"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_ADD_COMMENTS, m_codeEditor->IsEditable());
			toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), wxIcon(s_removeComment_xpm), _("Remove"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_codeEditor->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_SYSNTAX_CONTROL, _("Syntax control"), wxIcon(s_syntaxControl_xpm), _("Syntax"), wxItemKind::wxITEM_NORMAL);
			toolbar->EnableTool(wxID_SYSNTAX_CONTROL, m_codeEditor->IsEditable());
			toolbar->AddSeparator();
			toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), wxIcon(s_gotoLine_xpm), _("Goto"), wxItemKind::wxITEM_NORMAL);
			toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), wxIcon(s_proceduresFunctions_xpm), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
		}
	}
	else {
		for (auto controlClass : CValue::GetAvailableObjects(eObjectType::eObjectType_object_control)) {
			IControlValueAbstract* objectSingle = dynamic_cast<IControlValueAbstract*>(
				CValue::GetAvailableObject(controlClass)
				);
			wxASSERT(objectSingle);
			if (!objectSingle->IsControlSystem()) {
				wxBitmap controlImage = objectSingle->GetControlImage();
				if (controlImage.IsOk()) {
					toolbar->AddTool(nextId++, controlClass, controlImage, controlClass);
					toolbar->EnableTool(nextId - 1, m_visualEditor->IsEditable());
				}
			}
		}
	}

	toolbar->Bind(wxEVT_MENU, &CFormEditView::OnMenuClicked, this);
}

void CFormEditView::OnRemoveToolbar(wxAuiToolBar* toolbar)
{
	if (IsEditorActivate()) {
		toolbar->Unbind(wxEVT_MENU, &CFormEditView::OnMenuClicked, this);
	}
}

void CFormEditView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	if (activate) {
		if (IsDesignerActivate()) {
			if (mainFrame->GetMenuBar()->FindMenu(formName) == wxNOT_FOUND) {
				mainFrame->Connect(wxEVT_MENU, wxCommandEventHandler(CFormEditView::OnMenuClick), NULL, this);
				mainFrame->GetMenuBar()->Insert(formPos, CreateMenuForm(), formName);
			}
			m_visualEditor->ActivateObject();
		}
		else {
			if (mainFrame->GetMenuBar()->FindMenu(formName) != wxNOT_FOUND) {
				mainFrame->GetMenuBar()->Remove(formPos);
				mainFrame->Disconnect(wxEVT_MENU, wxCommandEventHandler(CFormEditView::OnMenuClick), NULL, this);
			}
		}
	}
	else {
		if (mainFrame->GetMenuBar()->FindMenu(formName) != wxNOT_FOUND) {
			mainFrame->GetMenuBar()->Remove(formPos);
			mainFrame->Disconnect(wxEVT_MENU, wxCommandEventHandler(CFormEditView::OnMenuClick), NULL, this);
		}
	}

	CView::OnActivateView(activate, activeView, deactiveView);
}

void CFormEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_visualEditor != NULL)
		m_visualEditor->RefreshEditor();

	if (m_codeEditor != NULL) {
		m_codeEditor->SetEditorSettings(mainFrame->GetEditorSettings());
		m_codeEditor->SetFontColorSettings(mainFrame->GetFontColorSettings());

		m_codeEditor->UpdateBreakpoints();
	}
}

void CFormEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CFormEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	m_visualEditor->DeactivateObject();
	return CView::OnClose(deleteWindow);
}

void CFormEditView::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	if (m_notebook->GetSelection() == 0) {
		m_visualEditor->CopyObject(m_visualEditor->GetSelectedObject());
	}
	else if (m_notebook->GetSelection() == 1) {
		m_codeEditor->Copy();
	}
}

void CFormEditView::OnPaste(wxCommandEvent& WXUNUSED(event))
{
	if (m_notebook->GetSelection() == 0) {
		m_visualEditor->PasteObject(m_visualEditor->GetSelectedObject());
	}
	else if (m_notebook->GetSelection() == 1) {
		m_codeEditor->Paste();
	}
}

void CFormEditView::OnSelectAll(wxCommandEvent& WXUNUSED(event))
{
	if (m_notebook->GetSelection() == 0) {
		//m_visualEditor->PasteObject(m_visualEditor->GetSelectedObject());
	}
	else if (m_notebook->GetSelection() == 1) {
		m_codeEditor->SelectAll();
	}
}

#include "frontend/codeEditor/codeEditorCtrlPrintOut.h"
#include "frontend/visualView/printout/formPrintOut.h"

wxPrintout* CFormEditView::OnCreatePrintout()
{
	if (IsEditorActivate()) {
		return new CCodeEditorPrintout(m_codeEditor, this->GetViewName());
	}

	return new CFormPrintout(m_visualEditor->GetVisualEditor());
}

#include "frontend/codeEditor/codeEditorParser.h"
#include "utils/stringUtils.h"

void CFormEditView::OnEventHandlerModified(wxFrameEventHandlerEvent& event)
{
	wxString prcArgs = ""; wxString eventName = event.GetValue();

	if (eventName.IsEmpty()) {
		return;
	}

	for (auto args : event.GetArgs()) {
		if (!prcArgs.IsEmpty()) {
			prcArgs += ", ";
		}
		prcArgs += args;
	}

	CParserModule parser; bool procFounded = false;

	unsigned int lineStart = m_codeEditor->GetLineCount();
	unsigned int lineEnd = lineStart;

	if (parser.ParseModule(m_codeEditor->GetText())) {
		for (auto content : parser.GetAllContent()) {

			if (content.eType == eContentType::eProcedure ||
				content.eType == eContentType::eFunction ||
				content.eType == eContentType::eExportProcedure ||
				content.eType == eContentType::eExportFunction) {

				wxString oldValue = event.GetPrevValue();

				lineStart = content.nLineStart; lineEnd = content.nLineEnd;

				if (StringUtils::CompareString(content.sName, oldValue)
					&& !StringUtils::CompareString(content.sName, event.GetValue())) {

					int answer = wxMessageBox(_("Do you rename an existing procedure?"), _("Rename"), wxYES_NO | wxCENTRE);
					if (answer == wxYES) {
						wxString currentLine = m_codeEditor->GetLine(content.nLineStart);
						currentLine.Replace(oldValue, event.GetValue());
						m_codeEditor->Replace(m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1, m_codeEditor->GetLineEndPosition(content.nLineStart) + 1, currentLine);
						procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
					}
					break;
				}
				else if (StringUtils::CompareString(content.sName, event.GetValue())) {
					procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
					break;
				}
			}
		}
	}

	if (!procFounded) {
		int endPos = m_codeEditor->GetLineEndPosition(lineEnd);
		wxString offset = endPos > 0 ?
			"\r\n\r\n" : "";
		m_codeEditor->Replace(endPos, endPos,
			offset +
			"procedure " + event.GetValue() + "(" + prcArgs + ")\r\n"
			"\t\r\n"
			"endProcedure"
		);
		int patchLine = endPos > 0 ?
			2 : -1;
		lineStart = lineEnd + patchLine;
	}

	m_codeEditor->GotoLine(lineStart);
	m_notebook->SetSelection(1);
}

void CFormEditView::OnFind(wxFindDialogEvent& event)
{
	int wxflags = event.GetFlags();
	int sciflags = 0;

	if ((wxflags & wxFR_WHOLEWORD) != 0) {
		sciflags |= wxSTC_FIND_WHOLEWORD;
	}

	if ((wxflags & wxFR_MATCHCASE) != 0) {
		sciflags |= wxSTC_FIND_MATCHCASE;
	}

	int result;
	if ((wxflags & wxFR_DOWN) != 0) {
		m_codeEditor->SetSelectionStart(m_codeEditor->GetSelectionEnd());
		m_codeEditor->SearchAnchor();
		result = m_codeEditor->SearchNext(sciflags, event.GetFindString());
	}
	else {
		m_codeEditor->SetSelectionEnd(m_codeEditor->GetSelectionStart());
		m_codeEditor->SearchAnchor();
		result = m_codeEditor->SearchPrev(sciflags, event.GetFindString());
	}

	if (wxSTC_INVALID_POSITION == result) {
		wxMessageBox(wxString::Format(_("\"%s\" not found!"), event.GetFindString().c_str()), _("Not Found!"), wxICON_ERROR, (wxWindow*)event.GetClientData());
	}
	else {
		m_codeEditor->EnsureCaretVisible();
	}
}

void CFormEditView::OnMenuClick(wxCommandEvent& event)
{
	if (event.GetId() == wxID_TEST_FORM)
		m_visualEditor->TestForm();

	event.Skip();
}

void CFormEditView::OnPageChanged(wxAuiNotebookEvent& event)
{
	int selection = event.GetSelection();

	if (selection == 0) {
		objectInspector->SelectObject(m_visualEditor->GetSelectedObject(), m_visualEditor->GetEventHandler());
	}
	else if (selection == 1) {
		CDocument* metaDoc = GetDocument();
		if (metaDoc != NULL) {
			IMetaObject* moduleObject = metaDoc->GetMetaObject();
			if (moduleObject != NULL) {
				objectInspector->SelectObject(moduleObject);
			}
		}
		m_codeEditor->SetSTCFocus(true);
	}

	Activate(true); event.Skip();
}

void CFormEditView::OnMenuClicked(wxCommandEvent& event)
{
	CDocument* docView = dynamic_cast<CDocument*>(m_viewDocument);

	if (IsEditorActivate()) {
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
		else if (event.GetId() == wxID_SYSNTAX_CONTROL) {
			IMetaObject* metaObject = docView->GetMetaObject();
			wxASSERT(metaObject);
			IMetadata* metaData = metaObject->GetMetadata();
			wxASSERT(metaData);
			IModuleManager* moduleManager = metaData->GetModuleManager();
			wxASSERT(moduleManager);
			IModuleInfo* dataRef = NULL;
			if (moduleManager->FindCompileModule(metaObject, dataRef)) {
				CCompileModule* m_compileModule = dataRef->GetCompileModule();
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
			CLineInput* lineInput = new CLineInput(m_codeEditor);
			int ret = lineInput->ShowModal();
			if (ret != wxNOT_FOUND) {
				m_codeEditor->SetFocus();
				m_codeEditor->GotoLine(ret - 1);
			}
		}
		else if (event.GetId() == wxID_PROCEDURES_FUNCTIONS) {
			CFunctionList* funcList = new CFunctionList(GetDocument(), m_codeEditor);
			int ret = funcList->ShowModal();
		}
	}
	else {
		wxAuiToolBar* m_tv = mainFrame->GetAdditionalToolbar();

		wxString name = m_tv->GetToolShortHelp(event.GetId());
		m_visualEditor->CreateObject(name);
	}

	//event.Skip();
}

void CFormEditView::InitializeFormDesigner(long flags)
{
	m_visualEditor = new CVisualEditorContextForm(GetDocument(), this, m_notebook);
	m_visualEditor->SetReadOnly(flags == wxDOC_READONLY);
}

void CFormEditView::InitializeCodeView(long flags)
{
	m_codeEditor = new CCodeEditorCtrl(GetDocument(), m_notebook, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	m_codeEditor->SetEditorSettings(mainFrame->GetEditorSettings());
	m_codeEditor->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_codeEditor->SetReadOnly(flags == wxDOC_READONLY);
	m_codeEditor->SetSTCFocus(true);
}

bool CFormEditView::LoadForm()
{
	return m_visualEditor->LoadForm() && m_codeEditor->LoadModule();
}

bool CFormEditView::SaveForm()
{
	return m_visualEditor->SaveForm() && m_codeEditor->SaveModule();
}