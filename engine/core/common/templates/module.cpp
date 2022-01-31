#include "module.h"
#include "module_cmd.h"
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
	m_code = new CAutocomplectionCtrl(doc, m_viewFrame, wxID_ANY,
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

extern wxImageList *GetImageList();

void CModuleView::OnCreateToolbar(wxAuiToolBar *m_toolbar)
{
	if (m_code == NULL)
		return;

	if (!m_toolbar->GetToolCount())
	{
		/*for (int i = 0; i < GetImageList()->GetImageCount(); i++)
		{
			wxBitmap m_bmp = GetImageList()->GetBitmap(i);

			wxString sFileName; sFileName << "D:/Icons/" << i;

			m_bmp.SaveFile(sFileName + ".bmp", wxBitmapType::wxBITMAP_TYPE_BMP);
			m_bmp.SaveFile(sFileName + ".png", wxBitmapType::wxBITMAP_TYPE_PNG);
			m_bmp.SaveFile(sFileName + ".ico", wxBitmapType::wxBITMAP_TYPE_ICO);
			m_bmp.SaveFile(sFileName + ".xpm", wxBitmapType::wxBITMAP_TYPE_XPM);
		}*/

		m_toolbar->AddTool(wxID_ADD_COMMENTS, _("Add comments"), GetImageList()->GetBitmap(622), _("Add"), wxItemKind::wxITEM_NORMAL);
		m_toolbar->EnableTool(wxID_ADD_COMMENTS, m_code->IsEditable());
		m_toolbar->AddTool(wxID_REMOVE_COMMENTS, _("Remove comments"), GetImageList()->GetBitmap(623), _("Remove"), wxItemKind::wxITEM_NORMAL);
		m_toolbar->EnableTool(wxID_REMOVE_COMMENTS, m_code->IsEditable());
		m_toolbar->AddSeparator();
		m_toolbar->AddTool(wxID_SYSNTAX_CONTROL, _("Syntax control"), GetImageList()->GetBitmap(624), _("Syntax"), wxItemKind::wxITEM_NORMAL);
		m_toolbar->EnableTool(wxID_SYSNTAX_CONTROL, m_code->IsEditable());
		m_toolbar->AddSeparator();
		m_toolbar->AddTool(wxID_GOTOLINE, _("Goto line"), GetImageList()->GetBitmap(626), _("Goto"), wxItemKind::wxITEM_NORMAL);
		//m_toolbar->EnableTool(wxID_GOTOLINE, m_code->IsEditable());
		m_toolbar->AddTool(wxID_PROCEDURES_FUNCTIONS, _("Procedures and functions"), GetImageList()->GetBitmap(627), _("Procedures and functions"), wxItemKind::wxITEM_NORMAL);
		//m_toolbar->EnableTool(wxID_PROCEDURES_FUNCTIONS, m_code->IsEditable());
	}

	m_toolbar->Bind(wxEVT_MENU, &CModuleView::OnMenuClicked, this);
}

void CModuleView::OnRemoveToolbar(wxAuiToolBar *m_toolbar)
{
	m_toolbar->Unbind(wxEVT_MENU, &CModuleView::OnMenuClicked, this);
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

#include "frontend/autocomplete/autoComplectionPrintOut.h"

wxPrintout *CModuleView::OnCreatePrintout()
{
	return new CAutocomplectionPrint(m_code, this->GetViewName());
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
		CLineInput *m_lineInput = new CLineInput(m_code);
		int ret = m_lineInput->ShowModal();
		if (ret != wxNOT_FOUND) {
			m_code->SetFocus();
			m_code->GotoLine(ret - 1);
		}
	}
	else if (event.GetId() == wxID_PROCEDURES_FUNCTIONS) {
		CFunctionList *m_funcList = new CFunctionList(GetDocument(), m_code);
		int ret = m_funcList->ShowModal();
	}

	//event.Skip();
}

// ----------------------------------------------------------------------------
// CModuleDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CModuleDocument, CDocument);

CCommandProcessor *CModuleDocument::CreateCommandProcessor()
{
	CAutocomplectionCtrl* m_context = GetTextCtrl();
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
	CAutocomplectionCtrl *autocomplete = GetTextCtrl();

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
	CAutocomplectionCtrl *autocomplete = GetTextCtrl();

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

CAutocomplectionCtrl* CModuleEditDocument::GetTextCtrl() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CModuleView)->GetText() : NULL;
}

void CModuleEditDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint)
{
	CAutocomplectionCtrl *autoComplete = GetTextCtrl();
	wxASSERT(autoComplete);
	autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
}