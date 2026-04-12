////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report manager 
////////////////////////////////////////////////////////////////////////////

#include <wx/except.h>
#include <wx/docview.h>
#include <wx/cmdproc.h>
#include <wx/config.h>

#if wxUSE_PRINTING_ARCHITECTURE
#include <wx/paper.h>
#endif // wxUSE_PRINTING_ARCHITECTURE

#include "docManager.h"

//common templates 
#include "frontend/docView/templates/docViewText.h"
#include "frontend/docView/templates/docViewSpreadsheet.h"
#include "frontend/docView/templates/docViewHelp.h"

#include "backend/metadataConfiguration.h"

wxBEGIN_EVENT_TABLE(ibMetaDocManager, wxDocManager)

EVT_MENU(wxID_OPEN, ibMetaDocManager::OnFileOpen)
EVT_MENU(wxID_CLOSE, ibMetaDocManager::OnFileClose)
EVT_MENU(wxID_CLOSE_ALL, ibMetaDocManager::OnFileCloseAll)
EVT_MENU(wxID_REVERT, ibMetaDocManager::OnFileRevert)
EVT_MENU(wxID_NEW, ibMetaDocManager::OnFileNew)
EVT_MENU(wxID_SAVE, ibMetaDocManager::OnFileSave)
EVT_MENU(wxID_SAVEAS, ibMetaDocManager::OnFileSaveAs)
EVT_MENU(wxID_UNDO, ibMetaDocManager::OnUndo)
EVT_MENU(wxID_REDO, ibMetaDocManager::OnRedo)

// We don't know in advance how many items can there be in the MRU files
// list so set up OnMRUFile() as a handler for all menu events and do the
// check for the id of the menu item clicked inside it.
EVT_MENU(wxID_ANY, ibMetaDocManager::OnMRUFile)

EVT_UPDATE_UI(wxID_OPEN, ibMetaDocManager::OnUpdateFileOpen)
EVT_UPDATE_UI(wxID_CLOSE, ibMetaDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_CLOSE_ALL, ibMetaDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_REVERT, ibMetaDocManager::OnUpdateFileRevert)
EVT_UPDATE_UI(wxID_NEW, ibMetaDocManager::OnUpdateFileNew)
EVT_UPDATE_UI(wxID_SAVE, ibMetaDocManager::OnUpdateFileSave)
EVT_UPDATE_UI(wxID_SAVEAS, ibMetaDocManager::OnUpdateFileSaveAs)
EVT_UPDATE_UI(wxID_UNDO, ibMetaDocManager::OnUpdateUndo)
EVT_UPDATE_UI(wxID_REDO, ibMetaDocManager::OnUpdateRedo)

#if wxUSE_PRINTING_ARCHITECTURE
EVT_MENU(wxID_PRINT, ibMetaDocManager::OnPrint)
EVT_MENU(wxID_PREVIEW, ibMetaDocManager::OnPreview)
EVT_MENU(wxID_PRINT_SETUP, ibMetaDocManager::OnPageSetup)

EVT_UPDATE_UI(wxID_PRINT, ibMetaDocManager::OnUpdateDisableIfNoDoc)
EVT_UPDATE_UI(wxID_PREVIEW, ibMetaDocManager::OnUpdateDisableIfNoDoc)
// NB: we keep "Print setup" menu item always enabled as it can be used
//     even without an active document
#endif // wxUSE_PRINTING_ARCHITECTURE

EVT_MENU(wxID_FIND, ibMetaDocManager::OnFindDialog)

wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibMetaDocManager, wxDocManager);

//****************************************************************
//*                           ibMetaDocManager					 *
//****************************************************************

bool ibMetaDocManager::ibMetaDocTemplate::InitDocument(wxDocument* doc, const wxString& path, long flags)
{
	wxTRY
	{
		doc->SetFilename(path);
		doc->SetDocumentTemplate(this);
		GetDocumentManager()->AddDocument(doc);

		if (doc->OnCreate(path, flags)) {
			doc->SetCommandProcessor(doc->OnCreateCommandProcessor());
			return true;
		}

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
				doc->DeleteAllViews(); throw;
				)
}

////////////////////////////////////////////////////////////////////////////////////////////////

ibMetaDocManager::ibMetaDocManager()
	: wxDocManager(), m_findDialog(nullptr)
{
	AddDocTemplate(g_metaModuleCLSID,
		_("Text document"), wxT("*.txt;*.text"), wxT("txt;text"), _("Text Doc"), _("Text View"), CLASSINFO(ibTextFilibDocument), CLASSINFO(ibTextEditView), wxTEMPLATE_VISIBLE);

	AddDocTemplate(g_metaTemplateCLSID,
		_("Spreadsheet document"), wxT("*.oxl"), wxT("oxl"), _("Spreadsheet Doc"), _("Spreadsheet View"), CLASSINFO(ibSpreadsheetFilibDocument), CLASSINFO(ibSpreadsheetEditView), wxTEMPLATE_VISIBLE);

	AddDocTemplate(g_metaInterfaceCLSID,
		_("Help document"), wxT("*.hle"), wxT("hle"), _("Help Doc"), _("Help View"), CLASSINFO(ibHelpFilibDocument), CLASSINFO(ibHelpEditView), wxTEMPLATE_INVISIBLE);

#if wxUSE_PRINTING_ARCHITECTURE

	// initialize print data and setup
	wxPrintData printData;

	wxPrintPaperType* paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4);

	printData.SetPaperId(paper->GetId());
	printData.SetPaperSize(paper->GetSize());
	printData.SetOrientation(wxPORTRAIT);

	// copy over initial paper size from print record
	m_pageSetupDialogData.SetPrintData(printData);

#endif // wxUSE_PRINTING_ARCHITECTURE
}

ibMetaDocManager::~ibMetaDocManager()
{
	wxDELETE(m_findDialog);
}

void ibMetaDocManager::OnFileClose(wxCommandEvent& WXUNUSED(event))
{
	ibMetaDocument* doc = GetCurrentDocument();
	if (doc) {
		CloseDocument(doc);
	}
}

void ibMetaDocManager::OnFileCloseAll(wxCommandEvent& WXUNUSED(event))
{
	CloseDocuments(false);
}

void ibMetaDocManager::OnFileNew(wxCommandEvent& WXUNUSED(event))
{
	CreateNewDocument();
}

void ibMetaDocManager::OnFileOpen(wxCommandEvent& WXUNUSED(event))
{
	if (!CreateDocument(wxT(""), 0)) 
		OnOpenFileFailure();	
}

void ibMetaDocManager::OnFileRevert(wxCommandEvent& WXUNUSED(event))
{
	ibMetaDocument* doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->Revert();
}

void ibMetaDocManager::OnFileSave(wxCommandEvent& WXUNUSED(event))
{
	ibMetaDocument* doc = GetCurrentDocument();

	if (!doc) {

		if (activeMetaData != nullptr && activeMetaData->IsModified())
			activeMetaData->SaveDatabase();

		return;
	}

	doc->Save();
}

void ibMetaDocManager::OnFileSaveAs(wxCommandEvent& WXUNUSED(event))
{
	ibMetaDocument* doc = GetCurrentDocument();
	if (!doc)
		return;
	doc->SaveAs();
}

void ibMetaDocManager::OnMRUFile(wxCommandEvent& event)
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

void ibMetaDocManager::OnPrint(wxCommandEvent& event)
{
	wxDocManager::OnPrint(event);
}

void ibMetaDocManager::OnPreview(wxCommandEvent& event)
{
	wxDocManager::OnPreview(event);
}

void ibMetaDocManager::OnPageSetup(wxCommandEvent& event)
{
	wxDocManager::OnPageSetup(event);
}

#endif // wxUSE_PRINTING_ARCHITECTURE

void ibMetaDocManager::OnUndo(wxCommandEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Undo();
}

void ibMetaDocManager::OnRedo(wxCommandEvent& event)
{
	wxCommandProcessor* const cmdproc = GetCurrentCommandProcessor();
	if (!cmdproc) {
		event.Skip();
		return;
	}

	cmdproc->Redo();
}

#include "frontend/mainFrame/mainFrame.h"

void ibMetaDocManager::OnFindDialog(wxCommandEvent& event)
{
	if (nullptr == m_findDialog)
	{
		m_findDialog = new wxFindReplaceDialog(mainFrame, &m_findData, _("Find"));
		m_findDialog->Centre(wxCENTRE_ON_SCREEN | wxBOTH);

		m_findDialog->Bind(wxEVT_FIND, &ibMetaDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_NEXT, &ibMetaDocManager::OnFind, this);
		m_findDialog->Bind(wxEVT_FIND_CLOSE, &ibMetaDocManager::OnFindClose, this);
	}

	m_findDialog->Show(true);
}

void ibMetaDocManager::OnFindClose(wxFindDialogEvent& event)
{
	m_findDialog->Unbind(wxEVT_FIND, &ibMetaDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_NEXT, &ibMetaDocManager::OnFind, this);
	m_findDialog->Unbind(wxEVT_FIND_CLOSE, &ibMetaDocManager::OnFindClose, this);

	m_findDialog->Destroy();
	m_findDialog = nullptr;
}

void ibMetaDocManager::OnFind(wxFindDialogEvent& event)
{
	ibMetaDocument* currDocument = GetCurrentDocument();
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

void ibMetaDocManager::OnUpdateFileOpen(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileOpen) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void ibMetaDocManager::OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event)
{
	event.Enable(GetCurrentDocument() != nullptr);
}

void ibMetaDocManager::OnUpdateFileRevert(wxUpdateUIEvent& event)
{
	ibMetaDocument* doc = GetCurrentDocument();
	event.Enable(doc && doc->IsModified() && doc->GetDocumentSaved());
}

void ibMetaDocManager::OnUpdateFileNew(wxUpdateUIEvent& event)
{
	// CreateDocument() (which is called from OnFileNew) may succeed
	// only when there is at least a template:
	event.Enable(GetTemplates().GetCount() > 0);
}

void ibMetaDocManager::OnUpdateFileSave(wxUpdateUIEvent& event)
{
	ibMetaDocument* const doc = GetCurrentDocument();
	event.Enable(
		(doc && !doc->AlreadySaved()) ||
		(doc == nullptr && (activeMetaData != nullptr && activeMetaData->IsModified()))
	);
}

void ibMetaDocManager::OnUpdateFileSaveAs(wxUpdateUIEvent& event)
{
	ibMetaDocument* const doc = GetCurrentDocument();
	wxDocTemplate* const docTemplate = doc != nullptr ?
		doc->GetDocumentTemplate() : nullptr;

	bool enable_save_as = doc && !doc->IsChildDocument();

	if (docTemplate != nullptr &&
		(docTemplate->GetFlags() & wxTEMPLATE_SAVE_AS_FILE) != 0)
		enable_save_as = true;

	event.Enable(enable_save_as);
}

void ibMetaDocManager::OnUpdateUndo(wxUpdateUIEvent& event)
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

void ibMetaDocManager::OnUpdateRedo(wxUpdateUIEvent& event)
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

void ibMetaDocManager::OnUpdateSaveMetadata(wxUpdateUIEvent& event)
{
	event.Enable(activeMetaData != nullptr && activeMetaData->IsModified());
}

wxDocument* ibMetaDocManager::CreateDocument(const wxString& pathOrig, long flags)
{
	// this ought to be const but SelectDocumentType/Path() are not
	// const-correct and can't be changed as, being virtual, this risks
	// breaking user code overriding them
	wxDocTemplateVector templates;  //(GetVisibleTemplates(m_templates));

	for (auto docTempl : m_templateVector)
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
	//docNew->Activate();

	return docNew;
}

wxDocTemplate* ibMetaDocManager::SelectDocumentPath(wxDocTemplate** templates, int noTemplates, wxString& path, long flags, bool save)
{
#ifdef wxHAS_MULTIPLE_FILEDLG_FILTERS
	wxString descrBuf;

	for (int i = 0; i < noTemplates; i++)
	{
		if (templates[i]->IsVisible())
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
		if (templates[i]->IsVisible())
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

#include "frontend/win/dlgs/choiceTemplate.h"

wxDocTemplate* ibMetaDocManager::SelectDocumentType(wxDocTemplate** templates, int noTemplates, bool sort)
{
	int i;
	int n = 0;

	wxVector<CChoiceTemplateItem> choices;

	for (i = 0; i < noTemplates; i++)
	{
		if (templates[i]->GetFlags() == wxTEMPLATE_VISIBLE)
		{
			int j;
			bool want = true;

			for (j = 0; j < n; j++)
			{
				//filter out NOT unique documents + view combinations
				if (templates[i]->GetDocumentName() == choices[j].m_template->GetDocumentName() &&
					templates[i]->GetViewName() == choices[j].m_template->GetViewName()
					)
					want = false;
			}

			if (want)
			{
				wxDocTemplate* docTemplate = templates[i];
				auto iterator = std::find_if(m_templateVector.begin(), m_templateVector.end(),
					[docTemplate](const ibMetaDocManagerItem& value) { return value.m_docTemplate == docTemplate; });

				if (iterator != m_templateVector.end())
				{
					CChoiceTemplateItem entry;

					entry.m_template = docTemplate;
					entry.m_description = docTemplate->GetDescription();
					entry.m_icon = iterator->m_classIcon;

					choices.push_back(entry);
				}

				n++;
			}
		}
	}  // for

	if (sort)
	{
		// ascending sort
		// Yes, this will be slow, but template lists
		// are typically short.
		std::sort(choices.begin(), choices.end(),
			[](const CChoiceTemplateItem& item1, const CChoiceTemplateItem& item2) {
				return item1.m_description < item2.m_description; });
	}

	wxDocTemplate* theTemplate = NULL;

	switch (n)
	{
	case 0:
		// no visible templates, hence nothing to choose from
		theTemplate = nullptr;
		break;

	case 1:
		// don't propose the user to choose if he has no choice
		theTemplate = choices[0].m_template;
		break;

	default:

		// propose the user to choose one of several
		ibDialogChoiceTemplate dlg(choices);
		theTemplate = dlg.ShowModal() == wxID_OK ?
			dlg.GetSelectionData() : NULL;
	}

	return theTemplate;
}

wxDocTemplate* ibMetaDocManager::FindTemplateByDocClassInfo(const wxClassInfo* classInfo) const
{
	auto iterator = std::find_if(m_templateVector.begin(), m_templateVector.end(),
		[classInfo](const auto& value) {
			return value.m_docTemplate != nullptr &&
				classInfo == value.m_docTemplate->GetDocClassInfo();
		}
	);

	if (iterator != m_templateVector.end())
		return iterator->m_docTemplate;

	return nullptr;
}

void ibMetaDocManager::AddDocTemplate(
	const ibPictureID& id,
	const wxString& descr,
	const wxString& filter,
	const wxString& dir,
	const wxString& ext,
	const wxString& docTypeName,
	const wxString& viewTypeName,
	wxClassInfo* docClassInfo, wxClassInfo* viewClassInfo,
	long flags)
{
	ibMetaDocManagerItem entry;

	entry.m_className = docTypeName;
	entry.m_classDescr = descr;
	entry.m_docTemplate = new ibMetaDocTemplate(
		this,
		descr,
		filter,
		dir,
		ext,
		docTypeName,
		viewTypeName,
		docClassInfo,
		viewClassInfo,
		flags
	);

	entry.m_guidTemplate = wxNewUniqueGuid;
	entry.m_classIcon = ibBackendPicture::GetPictureAsIcon(id);

	m_templateVector.emplace_back(std::move(entry));
}

void ibMetaDocManager::AddDocTemplate(const ibPictureID& id,
	const wxString& descr,
	const wxString& filter,
	const wxString& ext,
	const wxString& docTypeName,
	const wxString& viewTypeName,
	wxClassInfo* docClassInfo,
	wxClassInfo* viewClassInfo,
	long flags)
{
	AddDocTemplate(id,
		descr, filter, wxEmptyString, ext, docTypeName, viewTypeName, docClassInfo, viewClassInfo, flags);
}

void ibMetaDocManager::AddDocTemplate(const ibClassID& clsid,
	const wxString& descr,
	const wxString& filter,
	const wxString& ext,
	wxClassInfo* docClassInfo,
	wxClassInfo* viewClassInfo)
{
	ibMetaDocManagerItem entry;

	entry.m_clsid = clsid;
	entry.m_docTemplate = new ibMetaDocTemplate(
		this,
		descr,
		filter,
		wxEmptyString,
		ext,
		wxEmptyString,
		wxEmptyString,
		docClassInfo,
		viewClassInfo,
		wxTEMPLATE_INVISIBLE | (!ext.IsEmpty() ? wxTEMPLATE_SAVE_AS_FILE : 0)
	);

	entry.m_guidTemplate = wxNewUniqueGuid;
	entry.m_classIcon = ibBackendPicture::GetPictureAsIcon(clsid);

	m_templateVector.push_back(std::move(entry));
}

void ibMetaDocManager::AddDocTemplate(const ibClassID& id,
	wxClassInfo* docClassInfo,
	wxClassInfo* viewClassInfo)
{
	AddDocTemplate(id,
		wxEmptyString, wxEmptyString, wxEmptyString, docClassInfo, viewClassInfo);
}

ibMetaDocument* ibMetaDocManager::GetCurrentDocument() const
{
	return dynamic_cast<ibMetaDocument*>(wxDocManager::GetCurrentDocument());
}

#include "backend/metaCollection/metaObject.h"

ibMetaDocument* ibMetaDocManager::OpenFormMDI(ibValueMetaObject* metaObject, long flags)
{
	return docManager->OpenForm(metaObject, nullptr, flags);
}

ibMetaDocument* ibMetaDocManager::OpenFormMDI(ibValueMetaObject* metaObject, ibMetaDocument* docParent, long flags)
{
	return docManager->OpenForm(metaObject, docParent, flags);
}

ibMetaDocument* ibMetaDocManager::OpenForm(ibValueMetaObject* metaObject, ibMetaDocument* docParent, long flags)
{
	for (auto currTemplate : m_templateVector) {

		if (currTemplate.m_clsid == metaObject->GetClassType()) {

			ibMetaDocTemplate* docTemplate = currTemplate.m_docTemplate;
			wxASSERT(docTemplate);
			wxClassInfo* docClassInfo = docTemplate->GetDocClassInfo();
			wxASSERT(docClassInfo);
			ibMetaDocument* newDocument = wxDynamicCast(
				docClassInfo->CreateObject(), ibMetaDocument
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
					newDocument->SetCommandProcessor(newDocument->OnCreateCommandProcessor());
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
				wxLogError(wxT("OpenForm: failed to create document view"));
				if (ibMetaDocManager::GetDocuments().Member(newDocument)) {
					newDocument->DeleteAllViews();
				}
			}
			return nullptr;
		}
	}

	return nullptr;
}

bool ibMetaDocManager::CloseDocument(wxDocument* doc, bool force)
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

bool ibMetaDocManager::CloseDocuments(bool force)
{
	wxList::compatibility_iterator node = m_docs.GetFirst();
	while (node) {
		ibMetaDocument* doc = (ibMetaDocument*)node->GetData();
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

bool ibMetaDocManager::Clear(bool force)
{
	if (!CloseDocuments(force))
		return false;
	m_currentView = nullptr;
	wxList::compatibility_iterator node = m_templates.GetFirst();
	while (node) {
		ibMetaDocTemplate* templ = (ibMetaDocTemplate*)node->GetData();
		wxList::compatibility_iterator next = node->GetNext();
		delete templ;
		node = next;
	}
	return true;
}