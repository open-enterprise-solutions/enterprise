////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report manager 
////////////////////////////////////////////////////////////////////////////

#include <wx/docview.h>
#include <wx/cmdproc.h>
#include <wx/config.h>

#if wxUSE_PRINTING_ARCHITECTURE
#include <wx/paper.h>
#endif // wxUSE_PRINTING_ARCHITECTURE

#include "docManager.h"

//common templates 
#include "frontend/docView/templates/text.h"
#include "frontend/docView/templates/template.h"

#include "backend/metadataConfiguration.h"


wxBEGIN_EVENT_TABLE(CMetaDocManager, wxDocManager)

EVT_MENU(wxID_OPEN, CMetaDocManager::OnFileOpen)
EVT_MENU(wxID_CLOSE, CMetaDocManager::OnFileClose)
EVT_MENU(wxID_CLOSE_ALL, CMetaDocManager::OnFileCloseAll)
EVT_MENU(wxID_REVERT, CMetaDocManager::OnFileRevert)
EVT_MENU(wxID_NEW, CMetaDocManager::OnFileNew)
EVT_MENU(wxID_SAVE, CMetaDocManager::OnFileSave)
EVT_MENU(wxID_SAVEAS, CMetaDocManager::OnFileSaveAs)
EVT_MENU(wxID_UNDO, CMetaDocManager::OnUndo)
EVT_MENU(wxID_REDO, CMetaDocManager::OnRedo)

// We don't know in advance how many items can there be in the MRU files
// list so set up OnMRUFile() as a handler for all menu events and do the
// check for the id of the menu item clicked inside it.
EVT_MENU(wxID_ANY, CMetaDocManager::OnMRUFile)

EVT_UPDATE_UI(wxID_OPEN, CMetaDocManager::OnUpdateFileOpen)
EVT_UPDATE_UI(wxID_CLOSE, CMetaDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_CLOSE_ALL, CMetaDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_REVERT, CMetaDocManager::OnUpdateFileRevert)
EVT_UPDATE_UI(wxID_NEW, CMetaDocManager::OnUpdateFileNew)
EVT_UPDATE_UI(wxID_SAVE, CMetaDocManager::OnUpdateFileSave)
EVT_UPDATE_UI(wxID_SAVEAS, CMetaDocManager::OnUpdateFileSaveAs)
EVT_UPDATE_UI(wxID_UNDO, CMetaDocManager::OnUpdateUndo)
EVT_UPDATE_UI(wxID_REDO, CMetaDocManager::OnUpdateRedo)

//EVT_UPDATE_UI(wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE, CMetaDocManager::OnUpdateSaveMetadata)
//EVT_UPDATE_UI(wxID_DESIGNER_UPDATE_METADATA, CMetaDocManager::OnUpdateSaveMetadata)

EVT_MENU(wxID_FIND, CMetaDocManager::OnFindDialog)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(CMetaDocManager, wxDocManager);

//****************************************************************
//*                           CMetaDocManager					 *
//****************************************************************

bool CMetaDocManager::CMetaDocTemplate::InitDocument(wxDocument* doc, const wxString& path, long flags)
{
	wxTRY
	{
		doc->SetFilename(path);
		doc->SetDocumentTemplate(this);
		GetDocumentManager()->AddDocument(doc);
		doc->SetCommandProcessor(doc->OnCreateCommandProcessor());

		if (doc->OnCreate(path, flags))
			return true;

		// The document may be already destroyed, this happens if its view
		// creation fails as then the view being created is destroyed
		// triggering the destruction of the document as this first view is
		// also the last one. However if OnCreate() fails for any reason other
		// than view creation failure, the document is still alive and we need
		// to clean it up ourselves to avoid having a zombie document.
		if (GetDocumentManager()->GetDocuments().Member(doc))
			doc->DeleteAllViews();

		return false;
	}
		wxCATCH_ALL(
			if (GetDocumentManager()->GetDocuments().Member(doc))
				doc->DeleteAllViews();
	throw;
		)
}

////////////////////////////////////////////////////////////////////////////////////////////////

CMetaDocManager::CMetaDocManager()
	: wxDocManager(), m_findDialog(nullptr)
{
	AddDocTemplate("Text document", "*.txt;*.text", "", "txt;text", "Text Doc", "Text View", CLASSINFO(CTextEditDocument), CLASSINFO(CTextEditView), wxTEMPLATE_VISIBLE);
	AddDocTemplate("Spreadsheet document", "*.oxl", "", "oxl", "Spreadsheet Doc", "Spreadsheet View", CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView), wxTEMPLATE_VISIBLE);

	//if (appData->DesignerMode()) {

	//	AddDocTemplate("External data processor", "*.edp", "", "edp", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorEditDocument), CLASSINFO(CDataProcessorEditView), wxTEMPLATE_VISIBLE);
	//	AddDocTemplate("External report", "*.erp", "", "erp", "Report Doc", "Report View", CLASSINFO(CReportEditDocument), CLASSINFO(CReportEditView), wxTEMPLATE_VISIBLE);
	//	AddDocTemplate("Configuration", "*.conf", "", "conf", "Configuration Doc", "Configuration View", CLASSINFO(CMetataEditDocument), CLASSINFO(CMetadataView), wxTEMPLATE_ONLY_OPEN);

	//	//common objects 
	//	AddDocTemplate(g_metaCommonModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	//	AddDocTemplate(g_metaCommonFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView));
	//	AddDocTemplate(g_metaCommonTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView));

	//	AddDocTemplate(g_metaInterfaceCLSID, CLASSINFO(CInterfaceEditDocument), CLASSINFO(CInterfaceEditView));
	//	AddDocTemplate(g_metaRoleCLSID, CLASSINFO(CRoleEditDocument), CLASSINFO(CRoleEditView));

	//	//advanced object
	//	AddDocTemplate(g_metaModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	//	AddDocTemplate(g_metaManagerCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleEditView));
	//	AddDocTemplate(g_metaFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView));
	//	AddDocTemplate(g_metaTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView));
	//}
	//else {
	//	AddDocTemplate("External data processor", "*.edp", "", "edp", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorDocument), CLASSINFO(CDataProcessorView), wxTEMPLATE_ONLY_OPEN);
	//	AddDocTemplate("External report", "*.erp", "", "erp", "Report Doc", "Report View", CLASSINFO(CReportDocument), CLASSINFO(CReportView), wxTEMPLATE_ONLY_OPEN);
	//}

#if wxUSE_PRINTING_ARCHITECTURE
	// initialize print data and setup
	wxPrintData m_printData;

	wxPrintPaperType* paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4);

	m_printData.SetPaperId(paper->GetId());
	m_printData.SetPaperSize(paper->GetSize());
	m_printData.SetOrientation(wxPORTRAIT);

	// copy over initial paper size from print record
	m_pageSetupDialogData.SetPrintData(m_printData);

#endif // wxUSE_PRINTING_ARCHITECTURE
}

CMetaDocManager::~CMetaDocManager()
{
	wxDELETE(m_findDialog);
}

void CMetaDocManager::OnFileClose(wxCommandEvent& WXUNUSED(event))
{
	CMetaDocument* doc = GetCurrentDocument();
	if (doc) {
		CloseDocument(doc);
	}
}

void CMetaDocManager::OnFileCloseAll(wxCommandEvent& WXUNUSED(event))
{
	CloseDocuments(false);
}

void CMetaDocManager::OnFileNew(wxCommandEvent& WXUNUSED(event))
{
	CreateNewDocument();
}

void CMetaDocManager::OnFileOpen(wxCommandEvent& WXUNUSED(event))
{
	if (!wxDocManager::CreateDocument(wxString())) {
		OnOpenFileFailure();
	}
}

void CMetaDocManager::OnFileRevert(wxCommandEvent& WXUNUSED(event))
{
	CMetaDocument* doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->Revert();
}

void CMetaDocManager::OnFileSave(wxCommandEvent& WXUNUSED(event))
{
	CMetaDocument* doc = GetCurrentDocument();

	if (!doc) {
		if (commonMetaData->IsModified()) {
			commonMetaData->SaveConfiguration();
		}
		return;
	}

	doc->Save();
}

void CMetaDocManager::OnFileSaveAs(wxCommandEvent& WXUNUSED(event))
{
	CMetaDocument* doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->SaveAs();
}

void CMetaDocManager::OnMRUFile(wxCommandEvent& event)
{
	if (m_fileHistory) {
		// Check if the id is in the range assigned to MRU list entries.
		const int id = event.GetId();
		if (id >= wxID_FILE1 &&
			id < wxID_FILE1 + static_cast<int>(m_fileHistory->GetCount()))
		{
			DoOpenMRUFile(id - wxID_FILE1);
			// Don't skip the event below.
			return;
		}
	}

	event.Skip();
}

#if wxUSE_PRINTING_ARCHITECTURE

void CMetaDocManager::OnPrint(wxCommandEvent& WXUNUSED(event))
{
	wxView* view = GetAnyUsableView();
	if (!view)
		return;

	wxPrintout* printout = view->OnCreatePrintout();
	if (printout) {
		wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());
		wxPrinter printer(&printDialogData);
		printer.Print(view->GetFrame(), printout, true);

		delete printout;
	}
}

void CMetaDocManager::OnPreview(wxCommandEvent& WXUNUSED(event))
{
	wxBusyCursor busy;
	wxView* view = GetAnyUsableView();
	if (!view)
		return;

	wxPrintout* printout = view->OnCreatePrintout();
	if (printout) {
		wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());

		// Pass two printout objects: for preview, and possible printing.
		wxPrintPreviewBase*
			preview = new wxPrintPreview(printout,
				view->OnCreatePrintout(),
				&printDialogData);

		if (!preview->IsOk()) {
			delete preview;
			wxLogError(_("Print preview creation failed."));
			return;
		}

		wxPreviewFrame* frame = CreatePreviewFrame(preview,
			wxTheApp->GetTopWindow(),
			_("Print Preview"));
		wxCHECK_RET(frame, "should create a print preview frame");

		frame->Centre(wxBOTH);
		frame->Initialize();
		frame->Show(true);
	}
}
#endif // wxUSE_PRINTING_ARCHITECTURE

void CMetaDocManager::OnUndo(wxCommandEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Undo();
}

void CMetaDocManager::OnRedo(wxCommandEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Redo();
}

#include "frontend/mainFrame/mainFrame.h"

void CMetaDocManager::OnFindDialog(wxCommandEvent& event)
{
	if (nullptr == m_findDialog)
	{
		m_findDialog = new wxFindReplaceDialog(mainFrame, &m_findData, wxT("Find"));
		m_findDialog->Centre(wxCENTRE_ON_SCREEN | wxBOTH);

		m_findDialog->Bind(wxEVT_FIND, &CMetaDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_NEXT, &CMetaDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_CLOSE, &CMetaDocManager::OnFindClose, this);
	}

	m_findDialog->Show(true);
}

void CMetaDocManager::OnFindClose(wxFindDialogEvent& event)
{
	m_findDialog->Unbind(wxEVT_FIND, &CMetaDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_NEXT, &CMetaDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_CLOSE, &CMetaDocManager::OnFindClose, this);

	m_findDialog->Destroy();
	m_findDialog = nullptr;
}

void CMetaDocManager::OnFind(wxFindDialogEvent& event)
{
	CMetaDocument* currDocument = GetCurrentDocument();
	if (currDocument != nullptr) {
		wxView* firstView = currDocument->GetFirstView();
		if (firstView != nullptr) {
			event.StopPropagation();
			event.SetClientData(m_findDialog);
			firstView->ProcessEvent(event);
		}
	}
}

// Handlers for UI update commands

void CMetaDocManager::OnUpdateFileOpen(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileOpen) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void CMetaDocManager::OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event)
{
	event.Enable(GetCurrentDocument() != nullptr);
}

void CMetaDocManager::OnUpdateFileRevert(wxUpdateUIEvent& event)
{
	CMetaDocument* doc = GetCurrentDocument();
	event.Enable(doc && doc->IsModified() && doc->GetDocumentSaved());
}

void CMetaDocManager::OnUpdateFileNew(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileNew) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void CMetaDocManager::OnUpdateFileSave(wxUpdateUIEvent& event)
{
	CMetaDocument* const doc = GetCurrentDocument();
	event.Enable(
		(doc && !doc->AlreadySaved()) ||
		(doc == nullptr && commonMetaData->IsModified())
	);
}

void CMetaDocManager::OnUpdateFileSaveAs(wxUpdateUIEvent& event)
{
	CMetaDocument* const doc = GetCurrentDocument();
	event.Enable(doc && !doc->IsChildDocument());
}

void CMetaDocManager::OnUpdateUndo(wxUpdateUIEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		// If we don't have any document at all, the menu item should really be
		// disabled.
		if (!GetCurrentDocument())
			event.Enable(false);
		else // But if we do have it, it might handle wxID_UNDO on its own
			event.Skip();
		return;
	}
	event.Enable(cmdproc->CanUndo());
	cmdproc->SetMenuStrings();
}

void CMetaDocManager::OnUpdateRedo(wxUpdateUIEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		// Use same logic as in OnUpdateUndo() above.
		if (!GetCurrentDocument())
			event.Enable(false);
		else
			event.Skip();
		return;
	}
	event.Enable(cmdproc->CanRedo());
	cmdproc->SetMenuStrings();
}

void CMetaDocManager::OnUpdateSaveMetadata(wxUpdateUIEvent& event)
{
	event.Enable(commonMetaData->IsModified());
}

wxDocument* CMetaDocManager::CreateDocument(const wxString& pathOrig, long flags)
{
	// this ought to be const but SelectDocumentType/Path() are not
	// const-correct and can't be changed as, being virtual, this risks
	// breaking user code overriding them
	wxDocTemplateVector templates;  //(GetVisibleTemplates(m_templates));

	for (auto docTempl : m_metaTemplates)
	{
		if (!docTempl.m_docTemplate->IsVisible())
			continue;

		templates.push_back(docTempl.m_docTemplate);
	}

	const size_t numTemplates = templates.size();
	if (!numTemplates) {
		// no templates can be used, can't create document
		return nullptr;
	}

	// normally user should select the template to use but wxDOC_SILENT flag we
	// choose one ourselves
	wxString path = pathOrig;   // may be modified below
	wxDocTemplate* temp;
	if (flags & wxDOC_SILENT) {
		wxASSERT_MSG(!path.empty(),
			"using empty path with wxDOC_SILENT doesn't make sense");

		temp = FindTemplateForPath(path);
		if (!temp) {
			wxLogWarning(_("The format of file '%s' couldn't be determined."),
				path);
		}
	}
	else {// not silent, ask the user
		// for the new file we need just the template, for an existing one we
		// need the template and the path, unless it's already specified
		if ((flags & wxDOC_NEW) || !path.empty())
			temp = SelectDocumentType(&templates[0], numTemplates);
		else
			temp = SelectDocumentPath(&templates[0], numTemplates, path, flags);
	}

	if (!temp)
		return nullptr;

	// check whether the document with this path is already opened
	if (!path.empty()) {
		wxDocument* const doc = FindDocumentByPath(path);

		if (doc) {
			// file already open, just activate it and return
			doc->Activate();
			return doc;
		}
	}

	// no, we need to create a new document


	// if we've reached the max number of docs, close the first one.
	if ((int)GetDocuments().GetCount() >= m_maxDocsOpen)
	{
		if (!CloseDocument((wxDocument*)GetDocuments().GetFirst()->GetData()))
		{
			// can't open the new document if closing the old one failed
			return nullptr;
		}
	}

	// do create and initialize the new document finally
	wxDocument* const docNew = temp->CreateDocument(path, flags);
	if (!docNew)
		return nullptr;

	docNew->SetDocumentName(temp->GetDocumentName());

	wxTRY
	{
		// call the appropriate function depending on whether we're creating a
		// new file or opening an existing one
		if (!(flags & wxDOC_NEW ? docNew->OnNewDocument()
								 : docNew->OnOpenDocument(path)))
		{
			docNew->DeleteAllViews();
			return nullptr;
		}
	}
	wxCATCH_ALL(docNew->DeleteAllViews(); throw; )

		// add the successfully opened file to MRU, but only if we're going to be
		// able to reopen it successfully later which requires the template for
		// this document to be retrievable from the file extension
		if (!(flags & wxDOC_NEW) && temp->FileMatchesTemplate(path))
			AddFileToHistory(path);

	// at least under Mac (where views are top level windows) it seems to be
	// necessary to manually activate the new document to bring it to the
	// forefront -- and it shouldn't hurt doing this under the other platforms
	docNew->Activate();

	return docNew;
}

wxDocTemplate* CMetaDocManager::SelectDocumentPath(wxDocTemplate** templates, int noTemplates, wxString& path, long flags, bool save)
{
#ifdef wxHAS_MULTIPLE_FILEDLG_FILTERS
	wxString descrBuf;

	for (int i = 0; i < noTemplates; i++)
	{
		if (templates[i]->GetFlags() == wxTEMPLATE_VISIBLE
			|| templates[i]->GetFlags() == wxTEMPLATE_ONLY_OPEN)
		{
			if (!descrBuf.empty())
				descrBuf << wxT(";");

			descrBuf << templates[i]->GetFileFilter();
		}
	}

	descrBuf = _("OES files (") + descrBuf + wxT(") |");

	for (int i = 0; i < noTemplates; i++)
	{
		if (templates[i]->IsVisible())
		{
			descrBuf << templates[i]->GetFileFilter() << wxT(";");
		}
	}

	for (int i = 0; i < noTemplates; i++)
	{
		if (templates[i]->GetFlags() == wxTEMPLATE_VISIBLE
			|| templates[i]->GetFlags() == wxTEMPLATE_ONLY_OPEN)
		{
			// add a '|' to separate this filter from the previous one
			if (!descrBuf.empty())
				descrBuf << wxT('|');

			descrBuf << templates[i]->GetDescription()
				<< wxT(" (") << templates[i]->GetFileFilter() << wxT(") |")
				<< templates[i]->GetFileFilter();
		}
	}
#else
	wxString descrBuf = wxT("*.*");
	wxUnusedVar(noTemplates);
#endif

	int FilterIndex = -1;

	wxString pathTmp = wxFileSelectorEx(_("Open File"),
		GetLastDirectory(),
		wxEmptyString,
		&FilterIndex,
		descrBuf,
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	wxDocTemplate* theTemplate = nullptr;
	if (!pathTmp.empty())
	{
		if (!wxFileExists(pathTmp))
		{
			wxString msgTitle;
			if (!wxTheApp->GetAppDisplayName().empty())
				msgTitle = wxTheApp->GetAppDisplayName();
			else
				msgTitle = wxString(_("File error"));

			wxMessageBox(_("Sorry, could not open this file."),
				msgTitle,
				wxOK | wxICON_EXCLAMATION | wxCENTRE);

			path.clear();
			return nullptr;
		}

		SetLastDirectory(wxPathOnly(pathTmp));

		path = pathTmp;

		// first choose the template using the extension, if this fails (i.e.
		// wxFileSelectorEx() didn't fill it), then use the path
		if (FilterIndex != -1)
		{
			theTemplate = templates[FilterIndex];
			if (theTemplate)
			{
				// But don't use this template if it doesn't match the path as
				// can happen if the user specified the extension explicitly
				// but didn't bother changing the filter.
				if (!theTemplate->FileMatchesTemplate(path))
					theTemplate = nullptr;
			}
		}

		if (!theTemplate)
			theTemplate = FindTemplateForPath(path);
		if (!theTemplate)
		{
			// Since we do not add files with non-default extensions to the
			// file history this can only happen if the application changes the
			// allowed templates in runtime.
			wxMessageBox(_("Sorry, the format for this file is unknown."),
				_("Open File"),
				wxOK | wxICON_EXCLAMATION | wxCENTRE);
		}
	}
	else
	{
		path.clear();
	}

	return theTemplate;
}

#include <wx/scopedarray.h>

wxDocTemplate* CMetaDocManager::SelectDocumentType(wxDocTemplate** templates, int noTemplates, bool sort)
{
	wxArrayString strings;
	wxScopedArray<wxDocTemplate*> data(noTemplates);
	int i;
	int n = 0;

	for (i = 0; i < noTemplates; i++)
	{
		if (templates[i]->GetFlags() == wxTEMPLATE_VISIBLE)
		{
			int j;
			bool want = true;

			for (j = 0; j < n; j++)
			{
				//filter out NOT unique documents + view combinations
				if (templates[i]->GetDocumentName() == data[j]->GetDocumentName() &&
					templates[i]->GetViewName() == data[j]->GetViewName()
					)
					want = false;
			}

			if (want)
			{
				strings.Add(templates[i]->GetDescription());

				data[n] = templates[i];
				n++;
			}
		}
	}  // for

	if (sort)
	{
		strings.Sort(); // ascending sort
		// Yes, this will be slow, but template lists
		// are typically short.
		int j;
		n = strings.Count();
		for (i = 0; i < n; i++)
		{
			for (j = 0; j < noTemplates; j++)
			{
				if (strings[i] == templates[j]->GetDescription())
					data[i] = templates[j];
			}
		}
	}

	wxDocTemplate* theTemplate;

	switch (n)
	{
	case 0:
		// no visible templates, hence nothing to choose from
		theTemplate = nullptr;
		break;

	case 1:
		// don't propose the user to choose if he has no choice
		theTemplate = data[0];
		break;

	default:
		// propose the user to choose one of several
		theTemplate = (wxDocTemplate*)wxGetSingleChoiceData
		(
			_("Templates:"),
			_("Select document type"),
			strings,
			(void**)data.get()
		);
	}

	return theTemplate;
}

void CMetaDocManager::AddDocTemplate(
	const wxString& descr,
	const wxString& filter,
	const wxString& dir,
	const wxString& ext,
	const wxString& docTypeName,
	const wxString& viewTypeName,
	wxClassInfo* docClassInfo, wxClassInfo* viewClassInfo,
	long flags)
{
	docElement_t data;
	data.m_className = docTypeName;
	data.m_classDescr = descr;
	data.m_docTemplate = new CMetaDocTemplate(this, descr, filter, dir, ext, docTypeName, viewTypeName, docClassInfo, viewClassInfo, flags);
	m_metaTemplates.push_back(data);
}

void CMetaDocManager::AddDocTemplate(
	const class_identifier_t& clsid,
	wxClassInfo* docClassInfo,
	wxClassInfo* viewClassInfo)
{
	docElement_t data;
	data.m_clsid = clsid;
	data.m_docTemplate = new CMetaDocTemplate(this, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, docClassInfo, viewClassInfo, wxTEMPLATE_INVISIBLE);
	m_metaTemplates.push_back(data);
}

CMetaDocument* CMetaDocManager::GetCurrentDocument() const
{
	return dynamic_cast<CMetaDocument*>(wxDocManager::GetCurrentDocument());
}

#include "backend/metaCollection/metaObject.h"

CMetaDocument* CMetaDocManager::OpenFormMDI(IMetaObject* metaObject, long flags)
{
	return docManager->OpenForm(metaObject, nullptr, flags);
}

CMetaDocument* CMetaDocManager::OpenFormMDI(IMetaObject* metaObject, CMetaDocument* docParent, long flags)
{
	return docManager->OpenForm(metaObject, docParent, flags);
}

CMetaDocument* CMetaDocManager::OpenForm(IMetaObject* metaObject, CMetaDocument* docParent, long flags)
{
	for (auto currTemplate : m_metaTemplates) {

		if (currTemplate.m_clsid == metaObject->GetClassType()) {

			CMetaDocTemplate* docTemplate = currTemplate.m_docTemplate;
			wxASSERT(docTemplate);
			wxClassInfo* docClassInfo = docTemplate->GetDocClassInfo();
			wxASSERT(docClassInfo);
			CMetaDocument* newDocument = wxDynamicCast(
				docClassInfo->CreateObject(), CMetaDocument
			);

			wxASSERT(newDocument);

			if (docParent != nullptr) {
				newDocument->SetDocParent(docParent);
			}

			try {

				newDocument->SetTitle(metaObject->GetModuleName());
				newDocument->SetFilename(metaObject->GetDocPath());
				newDocument->SetDocumentTemplate(docTemplate);
				newDocument->SetMetaObject(metaObject);

				//if doc has parent - special delete!
				if (docParent == nullptr) {
					wxDocManager::AddDocument(newDocument);
				}

				newDocument->SetIcon(metaObject->GetIcon());

				if (newDocument->OnCreate(metaObject->GetModuleName(), flags | wxDOC_NEW)) {
					wxCommandProcessor* cmdProc = newDocument->CreateCommandProcessor();
					if (cmdProc != nullptr)
						newDocument->SetCommandProcessor(cmdProc);
					newDocument->UpdateAllViews();
					newDocument->Activate();
					return newDocument;
				}

				// The document may be already destroyed, this happens if its view
				// creation fails as then the view being created is destroyed
				// triggering the destruction of the document as this first view is
				// also the last one. However if OnCreate() fails for any reason other
				// than view creation failure, the document is still alive and we need
				// to clean it up ourselves to avoid having a zombie document.
				newDocument->DeleteAllViews();
				return nullptr;
			}
			catch (...) {
				if (CMetaDocManager::GetDocuments().Member(newDocument)) {
					newDocument->DeleteAllViews();
				}
			}
			return nullptr;
		}
	}
	return nullptr;
}

bool CMetaDocManager::CloseDocument(wxDocument* doc, bool force)
{
	if (!doc->Close() && !force)
		return false;
	// To really force the document to close, we must ensure that it isn't
	// modified, otherwise it would ask the user about whether it should be
	// destroyed (again, it had been already done by Close() above) and might
	// not destroy it at all, while we must do it here.
	doc->Modify(false);
	// Implicitly deletes the document when
	// the last view is deleted
	doc->DeleteAllViews();
	wxASSERT(!m_docs.Member(doc));
	return true;
}

bool CMetaDocManager::CloseDocuments(bool force)
{
	wxList::compatibility_iterator node = m_docs.GetFirst();
	while (node) {
		CMetaDocument* doc = (CMetaDocument*)node->GetData();
		wxList::compatibility_iterator next = node->GetNext();
		if (!CloseDocument(doc, force))
			return false;
		// This assumes that documents are not connected in
		// any way, i.e. deleting one document does NOT
		// delete another.
		node = next;
	}
	return true;
}

bool CMetaDocManager::Clear(bool force)
{
	if (!CloseDocuments(force))
		return false;
	m_currentView = nullptr;
	wxList::compatibility_iterator node = m_templates.GetFirst();
	while (node) {
		CMetaDocTemplate* templ = (CMetaDocTemplate*)node->GetData();
		wxList::compatibility_iterator next = node->GetNext();
		delete templ;
		node = next;
	}
	return true;
}