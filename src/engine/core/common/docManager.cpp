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

#include "appData.h"
#include "cmdProc.h"
#include "docManager.h"

//defs mainwindow
#include "frontend/mainFrameDefs.h"

//common templates 
#include "common/templates/text.h"
#include "common/templates/module.h"
#include "common/templates/formDesigner.h"
#include "common/templates/template.h"

//files
#include "common/templates/dataProcessorFile.h"
#include "common/templates/dataReportFile.h"
#include "common/templates/metaFile.h"

#include "utils/stringUtils.h"

wxBEGIN_EVENT_TABLE(CDocManager, wxDocManager)

EVT_MENU(wxID_OPEN, CDocManager::OnFileOpen)
EVT_MENU(wxID_CLOSE, CDocManager::OnFileClose)
EVT_MENU(wxID_CLOSE_ALL, CDocManager::OnFileCloseAll)
EVT_MENU(wxID_REVERT, CDocManager::OnFileRevert)
EVT_MENU(wxID_NEW, CDocManager::OnFileNew)
EVT_MENU(wxID_SAVE, CDocManager::OnFileSave)
EVT_MENU(wxID_SAVEAS, CDocManager::OnFileSaveAs)
EVT_MENU(wxID_UNDO, CDocManager::OnUndo)
EVT_MENU(wxID_REDO, CDocManager::OnRedo)

// We don't know in advance how many items can there be in the MRU files
// list so set up OnMRUFile() as a handler for all menu events and do the
// check for the id of the menu item clicked inside it.
EVT_MENU(wxID_ANY, CDocManager::OnMRUFile)

EVT_UPDATE_UI(wxID_OPEN, CDocManager::OnUpdateFileOpen)
EVT_UPDATE_UI(wxID_CLOSE, CDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_CLOSE_ALL, CDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_REVERT, CDocManager::OnUpdateFileRevert)
EVT_UPDATE_UI(wxID_NEW, CDocManager::OnUpdateFileNew)
EVT_UPDATE_UI(wxID_SAVE, CDocManager::OnUpdateFileSave)
EVT_UPDATE_UI(wxID_SAVEAS, CDocManager::OnUpdateFileSaveAs)
EVT_UPDATE_UI(wxID_UNDO, CDocManager::OnUpdateUndo)
EVT_UPDATE_UI(wxID_REDO, CDocManager::OnUpdateRedo)

EVT_UPDATE_UI(wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE, CDocManager::OnUpdateSaveMetadata)
EVT_UPDATE_UI(wxID_DESIGNER_UPDATE_METADATA, CDocManager::OnUpdateSaveMetadata)

EVT_MENU(wxID_FIND, CDocManager::OnFindDialog)

wxEND_EVENT_TABLE()

void CDocManager::Destroy()
{
	CDocManager *const documentManager =
		CDocManager::GetDocumentManager();

	if (documentManager != NULL) {
		if (documentManager->CloseDocuments(true)) {
#if wxUSE_CONFIG
			documentManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
			delete documentManager;
		}
		else {
			wxASSERT(false);
		}
	}
}

wxIMPLEMENT_DYNAMIC_CLASS(CDocManager, wxDocManager);

//****************************************************************
//*                           CDocManager                     *
//****************************************************************

#include "metadata/metaObjects/metaObject.h"

CDocManager::CDocManager()
	: wxDocManager(), m_findDialog(NULL)
{
	AddDocTemplate("Text", "*.txt;*.text", "", "txt;text", "Text Doc", "Text View", CLASSINFO(CTextEditDocument), CLASSINFO(CTextEditView), wxTEMPLATE_VISIBLE, "Text", "Text", 317);
	AddDocTemplate("Grid", "*.grd", "", "grd", "Grid Doc", "Grid View", CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView), wxTEMPLATE_VISIBLE, "Template", "Template", 318);

	if (appData->DesignerMode())
	{
		AddDocTemplate("Data processor", "*.dpr", "", "dpr", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorEditDocument), CLASSINFO(CDataProcessorEditView), wxTEMPLATE_VISIBLE, "DataProcessor", "DataProcessor", 320);
		AddDocTemplate("Report", "*.rpt", "", "rpt", "Report Doc", "Report View", CLASSINFO(CReportEditDocument), CLASSINFO(CReportEditView), wxTEMPLATE_VISIBLE, "Report", "Report", 319);
		AddDocTemplate("Metadata", "*.mtd", "", "mtd", "Metadata Doc", "Metadata View", CLASSINFO(CMetataEditDocument), CLASSINFO(CMetadataView), wxTEMPLATE_ONLY_OPEN, "Metadata", "Metadata", 320);

		//common objects 
		AddDocTemplate(g_metaCommonModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleView), 601);
		AddDocTemplate(g_metaCommonFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView), 294);
		AddDocTemplate(g_metaCommonTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView), 318);

		//advanced object
		AddDocTemplate(g_metaModuleCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleView), 601);
		AddDocTemplate(g_metaManagerCLSID, CLASSINFO(CModuleEditDocument), CLASSINFO(CModuleView), 601);
		AddDocTemplate(g_metaFormCLSID, CLASSINFO(CFormEditDocument), CLASSINFO(CFormEditView), 294);
		AddDocTemplate(g_metaTemplateCLSID, CLASSINFO(CGridEditDocument), CLASSINFO(CGridEditView), 318);
	}
	else
	{
		AddDocTemplate("Data processor", "*.dpr", "", "dpr", "Data processor Doc", "Data processor View", CLASSINFO(CDataProcessorDocument), CLASSINFO(CDataProcessorView), wxTEMPLATE_ONLY_OPEN, "DataProcessor", "DataProcessor", 320);
		AddDocTemplate("Report", "*.rpt", "", "rpt", "Report Doc", "Report View", CLASSINFO(CReportDocument), CLASSINFO(CReportView), wxTEMPLATE_ONLY_OPEN, "Report", "Report", 319);
	}

#if wxUSE_PRINTING_ARCHITECTURE
	// initialize print data and setup
	wxPrintData m_printData;

	wxPrintPaperType *paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4);

	m_printData.SetPaperId(paper->GetId());
	m_printData.SetPaperSize(paper->GetSize());
	m_printData.SetOrientation(wxPORTRAIT);

	// copy over initial paper size from print record
	m_pageSetupDialogData.SetPrintData(m_printData);

#endif // wxUSE_PRINTING_ARCHITECTURE
}

CDocManager::~CDocManager() 
{
	wxDELETE(m_findDialog);
}

void CDocManager::OnFileClose(wxCommandEvent& WXUNUSED(event))
{
	CDocument *doc = GetCurrentDocument();
	if (doc) {
		CloseDocument(doc);
	}
}

void CDocManager::OnFileCloseAll(wxCommandEvent& WXUNUSED(event))
{
	CloseDocuments(false);
}

void CDocManager::OnFileNew(wxCommandEvent& WXUNUSED(event))
{
	CreateNewDocument();
}

void CDocManager::OnFileOpen(wxCommandEvent& WXUNUSED(event))
{
	if (!wxDocManager::CreateDocument(wxString())) {
		OnOpenFileFailure();
	}
}

void CDocManager::OnFileRevert(wxCommandEvent& WXUNUSED(event))
{
	CDocument *doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->Revert();
}

void CDocManager::OnFileSave(wxCommandEvent& WXUNUSED(event))
{
	CDocument *doc = GetCurrentDocument();

	if (!doc) {
		if (metadata->IsModified()) {
			metadata->SaveMetadata();
		}
		return;
	}

	doc->Save();
}

void CDocManager::OnFileSaveAs(wxCommandEvent& WXUNUSED(event))
{
	CDocument *doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->SaveAs();
}

void CDocManager::OnMRUFile(wxCommandEvent& event)
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

void CDocManager::OnPrint(wxCommandEvent& WXUNUSED(event))
{
	wxView *view = GetAnyUsableView();
	if (!view)
		return;

	wxPrintout *printout = view->OnCreatePrintout();
	if (printout) {
		wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());
		wxPrinter printer(&printDialogData);
		printer.Print(view->GetFrame(), printout, true);

		delete printout;
	}
}

void CDocManager::OnPreview(wxCommandEvent& WXUNUSED(event))
{
	wxBusyCursor busy;
	wxView *view = GetAnyUsableView();
	if (!view)
		return;

	wxPrintout *printout = view->OnCreatePrintout();
	if (printout) {
		wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());

		// Pass two printout objects: for preview, and possible printing.
		wxPrintPreviewBase *
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

void CDocManager::OnUndo(wxCommandEvent& event)
{
	CCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Undo();
}

void CDocManager::OnRedo(wxCommandEvent& event)
{
	CCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Redo();
}

#include "frontend/mainFrame.h"

void CDocManager::OnFindDialog(wxCommandEvent& event)
{
	if (NULL == m_findDialog)
	{
		m_findDialog = new wxFindReplaceDialog(mainFrame, &m_findData, wxT("Find"));
		m_findDialog->Centre(wxCENTRE_ON_SCREEN | wxBOTH);

		m_findDialog->Bind(wxEVT_FIND, &CDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_NEXT, &CDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_CLOSE, &CDocManager::OnFindClose, this);
	}

	m_findDialog->Show(true);
}

void CDocManager::OnFindClose(wxFindDialogEvent& event)
{
	m_findDialog->Unbind(wxEVT_FIND, &CDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_NEXT, &CDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_CLOSE, &CDocManager::OnFindClose, this);

	m_findDialog->Destroy();
	m_findDialog = NULL;
}

void CDocManager::OnFind(wxFindDialogEvent& event)
{
	CDocument *currDocument = GetCurrentDocument();

	if (currDocument) {

		wxView *firstView = currDocument->GetFirstView(); 
		if (firstView) {
	
			event.StopPropagation();
			event.SetClientData(m_findDialog);

			firstView->ProcessEvent(event);		
		}
	}
}

// Handlers for UI update commands

void CDocManager::OnUpdateFileOpen(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileOpen) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void CDocManager::OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event)
{
	event.Enable(GetCurrentDocument() != NULL);
}

void CDocManager::OnUpdateFileRevert(wxUpdateUIEvent& event)
{
	CDocument* doc = GetCurrentDocument();
	event.Enable(doc && doc->IsModified() && doc->GetDocumentSaved());
}

void CDocManager::OnUpdateFileNew(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileNew) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void CDocManager::OnUpdateFileSave(wxUpdateUIEvent& event)
{
	CDocument *const doc = GetCurrentDocument();
	event.Enable(
		(doc && !doc->AlreadySaved()) ||
		(doc == NULL && metadata->IsModified())
	);
}

void CDocManager::OnUpdateFileSaveAs(wxUpdateUIEvent& event)
{
	CDocument * const doc = GetCurrentDocument();
	event.Enable(doc && !doc->IsChildDocument());
}

void CDocManager::OnUpdateUndo(wxUpdateUIEvent& event)
{
	CCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
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

void CDocManager::OnUpdateRedo(wxUpdateUIEvent& event)
{
	CCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
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

void CDocManager::OnUpdateSaveMetadata(wxUpdateUIEvent &event)
{
	event.Enable(metadata->IsModified());
}

extern wxImageList *GetImageList();

wxDocument *CDocManager::CreateDocument(const wxString & pathOrig, long flags)
{
	// this ought to be const but SelectDocumentType/Path() are not
	// const-correct and can't be changed as, being virtual, this risks
	// breaking user code overriding them
	wxDocTemplateVector templates;  //(GetVisibleTemplates(m_templates));

	for (auto docTempl : m_aTemplates)
	{
		if (!docTempl.m_docTemplate->IsVisible())
			continue;

		templates.push_back(docTempl.m_docTemplate);
	}

	const size_t numTemplates = templates.size();

	if (!numTemplates)
	{
		// no templates can be used, can't create document
		return NULL;
	}

	// normally user should select the template to use but wxDOC_SILENT flag we
	// choose one ourselves
	wxString path = pathOrig;   // may be modified below
	wxDocTemplate *temp;
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
		return NULL;

	// check whether the document with this path is already opened
	if (!path.empty()) {
		wxDocument * const doc = FindDocumentByPath(path);

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
		if (!CloseDocument((wxDocument *)GetDocuments().GetFirst()->GetData()))
		{
			// can't open the new document if closing the old one failed
			return NULL;
		}
	}

	// do create and initialize the new document finally
	wxDocument * const docNew = temp->CreateDocument(path, flags);
	if (!docNew)
		return NULL;

	docNew->SetDocumentName(temp->GetDocumentName());

	wxTRY
	{
		// call the appropriate function depending on whether we're creating a
		// new file or opening an existing one
		if (!(flags & wxDOC_NEW ? docNew->OnNewDocument()
								 : docNew->OnOpenDocument(path)))
		{
			docNew->DeleteAllViews();
			return NULL;
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

wxDocTemplate *CDocManager::SelectDocumentPath(wxDocTemplate **templates, int noTemplates, wxString &path, long flags, bool save)
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

	wxDocTemplate *theTemplate = NULL;
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
			return NULL;
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
					theTemplate = NULL;
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

wxDocTemplate *CDocManager::SelectDocumentType(wxDocTemplate **templates, int noTemplates, bool sort)
{
	wxArrayString strings;
	wxScopedArray<wxDocTemplate *> data(noTemplates);
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

	wxDocTemplate *theTemplate;

	switch (n)
	{
	case 0:
		// no visible templates, hence nothing to choose from
		theTemplate = NULL;
		break;

	case 1:
		// don't propose the user to choose if he has no choice
		theTemplate = data[0];
		break;

	default:
		// propose the user to choose one of several
		theTemplate = (wxDocTemplate *)wxGetSingleChoiceData
		(
			_("Select a document template"),
			_("Templates"),
			strings,
			(void **)data.get()
		);
	}

	return theTemplate;
}

void CDocManager::AddDocTemplate(const wxString &descr, const wxString &filter, const wxString &dir, const wxString &ext, const wxString &docTypeName, const wxString &viewTypeName, wxClassInfo *docClassInfo, wxClassInfo *viewClassInfo, long flags, const wxString& sName, const wxString& sDescription, int nImage)
{
	CDocElement data;
	data.m_className = sName;
	data.m_classDescription = sDescription;
	data.m_nImage = nImage;
	data.m_docTemplate = new wxDocTemplate(this, descr, filter, dir, ext, docTypeName, viewTypeName, docClassInfo, viewClassInfo, flags);

	m_aTemplates.push_back(data);
}

void CDocManager::AddDocTemplate(const CLASS_ID &clsid, wxClassInfo *docClassInfo, wxClassInfo *viewClassInfo, int image)
{
	CDocElement data;
	data.m_clsid = clsid;
	data.m_nImage = image;
	data.m_docTemplate = new wxDocTemplate(this, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, docClassInfo, viewClassInfo, wxTEMPLATE_INVISIBLE);

	m_aTemplates.push_back(data);
}

CDocument* CDocManager::GetCurrentDocument() const
{
	return dynamic_cast<CDocument *>(wxDocManager::GetCurrentDocument());
}

CCommandProcessor * CDocManager::GetCurrentCommandProcessor() const
{
	return dynamic_cast<CCommandProcessor *>(wxDocManager::GetCurrentCommandProcessor());
}

#include "metadata/metaObjects/metaObject.h"

CDocument* CDocManager::OpenFormMDI(IMetaObject *metaObject, long flags)
{
	return docManager->OpenForm(metaObject, NULL, flags);
}

CDocument* CDocManager::OpenFormMDI(IMetaObject *metaObject, CDocument *docParent, long flags)
{
	return docManager->OpenForm(metaObject, docParent, flags);
}

CDocument* CDocManager::OpenForm(IMetaObject *metaObject, CDocument *docParent, long flags)
{
	for (auto currTemplate : m_aTemplates)
	{
		if (currTemplate.m_clsid == metaObject->GetClsid()) {

			wxDocTemplate *docTemplate = currTemplate.m_docTemplate;
			wxASSERT(docTemplate);
			wxClassInfo *docClassInfo = docTemplate->GetDocClassInfo();
			wxASSERT(docClassInfo);

			CDocument *newDocument = wxStaticCast(
				docClassInfo->CreateObject(), CDocument
			);

			wxASSERT(newDocument);

			if (docParent) {
				newDocument->SetDocParent(docParent);
			}

			try
			{
				newDocument->SetTitle(metaObject->GetModuleName());
				newDocument->SetFilename(metaObject->GetDocPath());
				newDocument->SetDocumentTemplate(docTemplate);
				newDocument->SetMetaObject(metaObject);

				//if doc has parent - special delete!
				if (!docParent) {
					wxDocManager::AddDocument(newDocument);
				}

				if (currTemplate.m_nImage > 0) newDocument->SetIcon(GetImageList()->GetIcon(currTemplate.m_nImage));

				if (newDocument->OnCreate(metaObject->GetModuleName(), flags | wxDOC_NEW)) {
					newDocument->SetCommandProcessor(newDocument->CreateCommandProcessor());
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

				return NULL;
			}
			catch (...)
			{
				if (CDocManager::GetDocuments().Member(newDocument)) {
					newDocument->DeleteAllViews();
				}
			}

			return NULL;
		}
	}

	return NULL;
}

bool CDocManager::CloseDocument(wxDocument* doc, bool force)
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

bool CDocManager::CloseDocuments(bool force)
{
	wxList::compatibility_iterator node = m_docs.GetFirst();
	while (node)
	{
		wxDocument* doc = (wxDocument*)node->GetData();
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

bool CDocManager::Clear(bool force)
{
	if (!CloseDocuments(force))
		return false;

	m_currentView = NULL;

	wxList::compatibility_iterator node = m_templates.GetFirst();

	while (node)
	{
		wxDocTemplate *templ = (wxDocTemplate*)node->GetData();
		wxList::compatibility_iterator next = node->GetNext();
		delete templ;
		node = next;
	}

	return true;
}