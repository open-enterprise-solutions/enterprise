////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : OES doc-manager wiring — bodies that the collapsed
//	              ibDocManager class (declared in docView.h) needs on top of
//	              the wx-fork base in docView.cpp. The file lives in both
//	              frontend (desktop) and wfrontend (web) builds; the few
//	              desktop-specific bodies (Find/Replace dialog parented on
//	              the AUI mainFrame) are reduced to stubs under OES_USE_WEB.
////////////////////////////////////////////////////////////////////////////

#include "frontend/docView/docManager.h"  // also brings docView.h transitively

#include "frontend/win/dlgs/choiceTemplate.h"
#include "backend/backend_picture.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"

#ifndef OES_USE_WEB
// Shared default templates registered through RegisterDefaultTemplates() —
// desktop only, since the editor TUs aren't linked into wfrontend.dll.
#include "frontend/docView/templates/docViewText.h"
#include "frontend/docView/templates/docViewSpreadsheet.h"
#include "frontend/docView/templates/docViewHelp.h"
#include "frontend/docView/templates/docViewAuditLog.h"
#endif

#ifndef OES_USE_WEB
// Desktop-only: Find/Replace dialog parents the wxFindReplaceDialog against
// the AUI MDI frame singleton. mainFrame.h pulls in ibFrontendDocMDIFrame
// + AUI + objectInspector — none of which exist on wfrontend.dll. Also the
// home of ibFrontendDocMDIFrame::CreateChildFrame used below to wire
// m_viewFrame on every view going through the template path.
#include "frontend/mainFrame/mainFrame.h"
#else
// Web-side counterpart of CreateChildFrame, called from the template path.
#include "frontend/web/webFrame.h"
#endif

#if wxUSE_DOC_VIEW_ARCHITECTURE

#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/scopeguard.h>
#include <memory>

wxIMPLEMENT_ABSTRACT_CLASS(ibDocTemplate, wxObject);

namespace
{

// Extracted from wx/src/common/docview.cpp's anonymous-namespace helper; used
// by ibDocTemplate::FileMatchesTemplate. Kept private to this TU so it
// doesn't leak through docManager.h.
wxString FindExtension(const wxString& path)
{
	wxString ext;
	wxFileName::SplitPath(path, nullptr, nullptr, &ext);

	// VZ: extensions are considered not case sensitive — is this really a good
	//     idea?
	return ext.MakeLower();
}

} // namespace

// ----------------------------------------------------------------------------
// ibDocTemplate (wx-fork base)
// ----------------------------------------------------------------------------

ibDocTemplate::ibDocTemplate(ibDocManager *manager,
                             const wxString& descr,
                             const wxString& filter,
                             const wxString& dir,
                             const wxString& ext,
                             const wxString& docTypeName,
                             const wxString& viewTypeName,
                             wxClassInfo *docClassInfo,
                             wxClassInfo *viewClassInfo,
                             long flags)
	: m_fileFilter(filter)
	, m_directory(dir)
	, m_description(descr)
	, m_defaultExt(ext)
	, m_docTypeName(docTypeName)
	, m_viewTypeName(viewTypeName)
{
	m_documentManager = manager;
	m_flags = flags;
	m_documentManager->AssociateTemplate(this);

	m_docClassInfo = docClassInfo;
	m_viewClassInfo = viewClassInfo;
}

ibDocTemplate::~ibDocTemplate()
{
	m_documentManager->DisassociateTemplate(this);
}

// Tries to dynamically construct an object of the right class.
ibDocument *ibDocTemplate::CreateDocument(const wxString& path, long flags)
{
	// InitDocument() is supposed to delete the document object if its
	// initialization fails so don't use unique_ptr<> here: this is fragile
	// but unavoidable because the default implementation uses CreateView()
	// which may -- or not -- create a ibView and if it does create it and its
	// initialization fails then the view destructor will delete the document
	// (via RemoveView()) and as we can't distinguish between the two cases we
	// just have to assume that it always deletes it in case of failure
	ibDocument * const doc = DoCreateDocument();

	return doc && InitDocument(doc, path, flags) ? doc : nullptr;
}

bool
ibDocTemplate::InitDocument(ibDocument* doc, const wxString& path, long flags)
{
	wxScopeGuard guard = wxMakeGuard([&, this]()
	{
		// The document may be already destroyed, this happens if its view
		// creation fails as then the view being created is destroyed
		// triggering the destruction of the document as this first view is
		// also the last one. However if OnCreate() fails for any reason other
		// than view creation failure, the document is still alive and we need
		// to clean it up ourselves to avoid having a zombie document.
		if (GetDocumentManager()->GetDocuments().Member(doc))
			doc->DeleteAllViews();
	});

	doc->SetFilename(path);
	doc->SetDocumentTemplate(this);
	GetDocumentManager()->AddDocument(doc);
	doc->SetCommandProcessor(doc->OnCreateCommandProcessor());

	if (!doc->OnCreate(path, flags))
		return false;

	guard.Dismiss();

	return true;
}

ibView *ibDocTemplate::CreateView(ibDocument *doc, long flags)
{
	std::unique_ptr<ibView> view(DoCreateView());
	if (!view)
		return nullptr;

	view->SetDocument(doc);

	// Child-frame creation — lifted up from ibMetaDocument::OnCreate so any
	// document going through the template path (AuditLog, Text, Help, …)
	// gets m_viewFrame populated BEFORE view->OnCreate runs. Otherwise
	// views that build their layout against m_viewFrame would crash on a
	// null parent. The meta path bypasses this CreateView entirely (its
	// own OnCreate calls CreateChildFrame itself), so there is no double
	// creation.
#ifdef OES_USE_WEB
	ibWebFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, 0);
#else
	bool createModal = false;
	for (wxWindow* window : wxTopLevelWindows) {
		if (window->IsKindOf(CLASSINFO(wxDialog))) {
			if (((wxDialog*)window)->IsModal()) {
				createModal = true;
				break;
			}
		}
	}

	long style = wxDEFAULT_FRAME_STYLE;
	if (createModal) style |= wxCREATE_SDI_FRAME;

	ibFrontendDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);
#endif

	if (!view->OnCreate(doc, flags))
		return nullptr;

	// Reveal the frame now that the view has built its content. Desktop
	// shows the ibAuiDocChildFrame; web flips the tab's shown state via
	// the per-view m_webFrame back-pointer.
	view->ShowFrame();

	return view.release();
}

// The default (very primitive) format detection: check is the extension is
// that of the template
bool ibDocTemplate::FileMatchesTemplate(const wxString& path)
{
	wxStringTokenizer parser(GetFileFilter(), wxT(";"));
	wxString anything = wxT("*");
	while (parser.HasMoreTokens())
	{
		wxString filter = parser.GetNextToken();
		wxString filterExt = FindExtension(filter);
		if (filter.IsSameAs(anything) ||
		    filterExt.IsSameAs(anything) ||
		    filterExt.IsSameAs(FindExtension(path)))
			return true;
	}
	return GetDefaultExtension().IsSameAs(FindExtension(path));
}

ibDocument *ibDocTemplate::DoCreateDocument()
{
	if (!m_docClassInfo)
		return nullptr;

	return static_cast<ibDocument *>(m_docClassInfo->CreateObject());
}

ibView *ibDocTemplate::DoCreateView()
{
	if (!m_viewClassInfo)
		return nullptr;

	return static_cast<ibView *>(m_viewClassInfo->CreateObject());
}

ibDocTemplateVector ibDocManager::GetTemplatesVector() const
{
	return m_templates.AsVector<ibDocTemplate*>();
}

// ----------------------------------------------------------------------------
// ibMetaDocTemplate — plain C++ class, no wxRTTI registration (templates are
// constructed directly through AddDocTemplate, never via dynamic factory).
// ----------------------------------------------------------------------------

ibMetaDocTemplate::ibMetaDocTemplate(ibDocManager* manager,
                                     const wxString& descr,
                                     const wxString& filter,
                                     const wxString& dir,
                                     const wxString& ext,
                                     const wxString& docTypeName,
                                     const wxString& viewTypeName,
                                     wxClassInfo* docClassInfo,
                                     wxClassInfo* viewClassInfo,
                                     long flags)
	: ibDocTemplate(manager, descr, filter, dir, ext,
	                docTypeName, viewTypeName,
	                docClassInfo, viewClassInfo, flags)
	, m_guidTemplate(wxNewUniqueGuid)
{
	// m_clsid stays default-constructed; SetClassID populates it for
	// metadata-keyed templates registered through AddDocTemplate(ibClassID,...).
}

bool ibMetaDocTemplate::InitDocument(ibDocument* doc, const wxString& path, long flags)
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

		// OnCreate failed: the document may already have been destroyed by
		// view-creation failure (its first view's death takes the document
		// with it). If it survived, tear down its views explicitly so a
		// zombie document doesn't linger in m_docs.
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

// ----------------------------------------------------------------------------
// ibDocManager — OES extensions
// ----------------------------------------------------------------------------

void ibDocManager::RegisterDefaultTemplates()
{
#ifndef OES_USE_WEB
	// Body is desktop-only because the editor view classes
	// (ibTextEditView, ibSpreadsheetEditView, ibHelpEditView,
	// ibAuditLogView) are not compiled into wfrontend.dll — their
	// CLASSINFO references would fail to link there. Web doesn't
	// present a "Choose template" dialog so the registration is a
	// no-op on that build.

	// Text / Spreadsheet / Help — meta-bound templates keyed by CLSID.
	AddDocTemplate(g_metaModuleCLSID,
		_("Text document"), wxT("*.txt;*.text"), wxT("txt;text"),
		_("Text Doc"), _("Text View"),
		CLASSINFO(ibTextFileDocument), CLASSINFO(ibTextEditView),
		ibTEMPLATE_VISIBLE);

	AddDocTemplate(g_metaTemplateCLSID,
		_("Spreadsheet document"), wxT("*.oxl"), wxT("oxl"),
		_("Spreadsheet Doc"), _("Spreadsheet View"),
		CLASSINFO(ibSpreadsheetFileDocument), CLASSINFO(ibSpreadsheetEditView),
		ibTEMPLATE_VISIBLE);

	AddDocTemplate(g_metaInterfaceCLSID,
		_("Help document"), wxT("*.hle"), wxT("hle"),
		_("Help Doc"), _("Help View"),
		CLASSINFO(ibHelpFileDocument), CLASSINFO(ibHelpEditView),
		ibTEMPLATE_INVISIBLE);

	// Registration journal — plain ibDocTemplate (no CLSID, no
	// metaobject). Invisible to File→New; reached through
	// CreateDocument<ibAuditLogDocument>() from the AuditLog menu
	// handler in both Designer and Enterprise, which resolves via
	// FindTemplateByDocClassInfo over m_templates. The ctor itself
	// calls AssociateTemplate(this).
	new ibDocTemplate(this,
		_("Registration journal"),
		wxEmptyString, wxEmptyString, wxEmptyString,
		_("Audit Log Doc"), _("Audit Log View"),
		CLASSINFO(ibAuditLogDocument), CLASSINFO(ibAuditLogView),
		ibTEMPLATE_INVISIBLE);
#endif
}

void ibDocManager::OnFindDialog(wxCommandEvent& WXUNUSED(event))
{
#ifndef OES_USE_WEB
	if (m_findDialog == nullptr) {
		m_findDialog = new wxFindReplaceDialog(mainFrame, &m_findData, _("Find"));
		m_findDialog->Centre(wxCENTRE_ON_SCREEN | wxBOTH);

		m_findDialog->Bind(wxEVT_FIND,       &ibDocManager::OnFind,      this);
		m_findDialog->Bind(wxEVT_FIND_NEXT,  &ibDocManager::OnFind,      this);
		m_findDialog->Bind(wxEVT_FIND_CLOSE, &ibDocManager::OnFindClose, this);
	}

	m_findDialog->Show(true);
#else
	// Web stub: no Find menu binding on web; the EVT_MENU(wxID_FIND) row in
	// the base event table stays harmlessly inactive.
#endif
}

void ibDocManager::OnFindClose(wxFindDialogEvent& WXUNUSED(event))
{
#ifndef OES_USE_WEB
	m_findDialog->Unbind(wxEVT_FIND,       &ibDocManager::OnFind,      this);
	m_findDialog->Unbind(wxEVT_FIND_NEXT,  &ibDocManager::OnFind,      this);
	m_findDialog->Unbind(wxEVT_FIND_CLOSE, &ibDocManager::OnFindClose, this);

	m_findDialog->Destroy();
	m_findDialog = nullptr;
#endif
}

void ibDocManager::OnFind(wxFindDialogEvent& event)
{
#ifndef OES_USE_WEB
	ibMetaDocument* currDocument = GetCurrentMetaDocument();
	if (currDocument != nullptr) {
		ibView* firstView = currDocument->GetFirstView();
		if (firstView != nullptr) {
			event.StopPropagation();
			event.SetClientData(m_findDialog);
			firstView->ProcessEvent(event);
		}
	}
#endif
}

ibMetaDocument* ibDocManager::GetCurrentMetaDocument() const
{
	return dynamic_cast<ibMetaDocument*>(GetCurrentDocument());
}

// ----------------------------------------------------------------------------
// AddDocTemplate — meta-template registrations (cross-build; always reachable
// via the AssociateTemplate base path, just iterates the same m_templates).
// ----------------------------------------------------------------------------

void ibDocManager::AddDocTemplate(const ibPictureID& id,
                                  const wxString& descr,
                                  const wxString& filter,
                                  const wxString& dir,
                                  const wxString& ext,
                                  const wxString& docTypeName,
                                  const wxString& viewTypeName,
                                  wxClassInfo* docClassInfo,
                                  wxClassInfo* viewClassInfo,
                                  long flags)
{
	auto* docTemplate = new ibMetaDocTemplate(
		this, descr, filter, dir, ext, docTypeName, viewTypeName,
		docClassInfo, viewClassInfo, flags);

	docTemplate->SetClassIcon(ibBackendPicture::GetPictureAsIcon(id));

	AssociateTemplate(docTemplate);
}

void ibDocManager::AddDocTemplate(const ibPictureID& id,
                                  const wxString& descr,
                                  const wxString& filter,
                                  const wxString& ext,
                                  const wxString& docTypeName,
                                  const wxString& viewTypeName,
                                  wxClassInfo* docClassInfo,
                                  wxClassInfo* viewClassInfo,
                                  long flags)
{
	AddDocTemplate(id, descr, filter, wxEmptyString, ext,
	               docTypeName, viewTypeName,
	               docClassInfo, viewClassInfo, flags);
}

void ibDocManager::AddDocTemplate(const ibClassID& clsid,
                                  const wxString& descr,
                                  const wxString& filter,
                                  const wxString& ext,
                                  wxClassInfo* docClassInfo,
                                  wxClassInfo* viewClassInfo)
{
	// Tools / advanced-object templates: not exposed via File→New
	// (always INVISIBLE); SAVE_AS_FILE only when the template can produce
	// a stand-alone file.
	auto* docTemplate = new ibMetaDocTemplate(
		this, descr, filter, wxEmptyString, ext,
		wxEmptyString, wxEmptyString,
		docClassInfo, viewClassInfo,
		ibTEMPLATE_INVISIBLE | (!ext.IsEmpty() ? ibTEMPLATE_SAVE_AS_FILE : 0));

	docTemplate->SetClassID(clsid);
	docTemplate->SetClassIcon(ibBackendPicture::GetPictureAsIcon(clsid));

	AssociateTemplate(docTemplate);
}

void ibDocManager::AddDocTemplate(const ibClassID& clsid,
                                  wxClassInfo* docClassInfo,
                                  wxClassInfo* viewClassInfo)
{
	AddDocTemplate(clsid,
	               wxEmptyString, wxEmptyString, wxEmptyString,
	               docClassInfo, viewClassInfo);
}

ibMetaDocTemplate* ibDocManager::FindMetaTemplate(const ibClassID& clsid) const
{
	for (wxList::compatibility_iterator node = m_templates.GetFirst();
	     node != nullptr; node = node->GetNext())
	{
		auto* mt = dynamic_cast<ibMetaDocTemplate*>(
			static_cast<ibDocTemplate*>(node->GetData()));
		if (mt != nullptr && mt->GetClassID() == clsid)
			return mt;
	}
	return nullptr;
}

ibDocTemplate* ibDocManager::FindTemplateByDocClassInfo(const wxClassInfo* classInfo) const
{
	for (wxList::compatibility_iterator node = m_templates.GetFirst();
	     node != nullptr; node = node->GetNext())
	{
		auto* t = static_cast<ibDocTemplate*>(node->GetData());
		if (t != nullptr && t->GetDocClassInfo() == classInfo)
			return t;
	}
	return nullptr;
}

// ----------------------------------------------------------------------------
// OpenForm / OpenFormMDI — metadata-driven open entry points. Cross-build:
// desktop calls them from the tree/menu handlers; web doesn't call them
// (sessions own their tabs through ibWebFrame::m_tabs, not the manager).
// ----------------------------------------------------------------------------

ibMetaDocument* ibDocManager::OpenFormMDI(ibValueMetaObject* metaObject, long flags)
{
	return docManager->OpenForm(metaObject, nullptr, flags);
}

ibMetaDocument* ibDocManager::OpenFormMDI(ibValueMetaObject* metaObject,
                                          ibMetaDocument* docParent, long flags)
{
	return docManager->OpenForm(metaObject, docParent, flags);
}

ibMetaDocument* ibDocManager::OpenForm(ibValueMetaObject* metaObject,
                                       ibMetaDocument* docParent, long flags)
{
	ibMetaDocTemplate* docTemplate = FindMetaTemplate(metaObject->GetClassType());
	if (docTemplate == nullptr)
		return nullptr;

	wxClassInfo* docClassInfo = docTemplate->GetDocClassInfo();
	wxASSERT(docClassInfo);

	auto* newDocument = wxDynamicCast(docClassInfo->CreateObject(), ibMetaDocument);
	wxASSERT(newDocument);

	if (docParent != nullptr)
		newDocument->SetDocParent(docParent);

	try {
		newDocument->SetTitle(metaObject->GetModuleName());
		newDocument->SetFilename(metaObject->GetDocPath());
		newDocument->SetDocumentTemplate(docTemplate);
		newDocument->SetMetaObject(metaObject);

		// Owned documents (those with a parent) are managed by the parent,
		// not the global doc list — adding them here would double-delete on
		// close.
		if (docParent == nullptr)
			AddDocument(newDocument);

		newDocument->SetIcon(metaObject->GetIcon());

		if (newDocument->OnCreate(metaObject->GetModuleName(), flags | ibDOC_NEW)) {
			newDocument->SetCommandProcessor(newDocument->OnCreateCommandProcessor());
			return newDocument;
		}

		newDocument->DeleteAllViews();
		return nullptr;
	}
	catch (...) {
		wxLogError(wxT("OpenForm: failed to create document view"));
		if (GetDocuments().Member(newDocument))
			newDocument->DeleteAllViews();
	}
	return nullptr;
}

// ----------------------------------------------------------------------------
// ibMetaView — desktop-only activation wiring. Moved here from the former
// docViewCmd.cpp so all the OES wiring with web-side stubs lives in one file.
// ----------------------------------------------------------------------------

void ibMetaView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
#ifndef OES_USE_WEB
	// objectInspector is a designer-side panel tracking the "currently
	// selected" meta object. Web has no equivalent panel; the session's
	// active form is already tracked on ibWebFrame::m_activeTab.
	if (activate)
		objectInspector->SelectObject(GetDocument() ? GetDocument()->GetMetaObject() : nullptr);
#else
	(void)activate; (void)activeView; (void)deactiveView;
#endif
}

// ibView::Activate lives here (not in docView.cpp) because it reaches the
// desktop main frame (mainFrame / ibFrontendDocMDIFrame), whose headers are
// only pulled in on this side — same split as the rest of the metadata-aware
// wiring. Lifted up from ibMetaView so the form view (a plain ibView since the
// doc/view fork) also clears its activation on the main frame; otherwise the
// reduced ibView path skipped mainFrame->ActivateView and deactivation stuck.
void ibView::Activate(bool activate)
{
#ifndef OES_USE_WEB
	// Local name shadows the `docManager` macro deliberately — picks the
	// view's owning manager first, falls back to the singleton.
	ibDocManager* const mgr = m_viewDocument != nullptr ?
		m_viewDocument->GetDocumentManager() : ibDocManager::GetDocumentManager();

	if (mgr != nullptr && ibFrontendDocMDIFrame::GetFrame()) {
		mainFrame->ActivateView(this, activate);
		OnActivateView(activate, this, mgr->GetCurrentView());
		mgr->ActivateView(this, activate);
	}

	if (activate) wxLogDebug("! <debug> activate view %s", GetViewName());
	else wxLogDebug("! <debug> deactivate view %s", GetViewName());
#else
	// Web: activation is driven from ibWebFrame::SetActiveTab directly
	// on the session's tab list — no docManager round-trip needed.
	(void)activate;
#endif
}

#endif // wxUSE_DOC_VIEW_ARCHITECTURE
