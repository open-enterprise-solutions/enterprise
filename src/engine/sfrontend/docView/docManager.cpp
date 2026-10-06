////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : OES doc-manager wiring — the templates (ibDocTemplate,
//	              ibMetaDocTemplate) and the bodies the collapsed ibDocManager
//	              class (declared in docView.h) needs on top of the wx-fork
//	              base in docView.cpp: the meta-template registry and OpenForm.
////////////////////////////////////////////////////////////////////////////

#include "sfrontend/docView/docManager.h"  // also brings docView.h transitively

// The base set the ctor registers — the shared templates…
#include "sfrontend/docView/templates/docViewText.h"
#include "sfrontend/docView/templates/docViewSpreadsheet.h"
#include "sfrontend/docView/templates/docViewHelp.h"
#include "sfrontend/docView/templates/docViewAuditLog.h"
#include "sfrontend/docView/templates/docViewHomePage.h"

// …and the shared schemas.
#include "sfrontend/schema/activeUser.h"

#include "backend/backend_picture.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"

// The session's frame: ibClientFrame::CreateChildFrame gives a view going
// through the template path its tab.
#include "sfrontend/client/clientFrame.h"

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

	// Tab creation — lifted up from ibMetaDocument::OnCreate so any document
	// going through the template path gets m_viewFrame populated BEFORE
	// view->OnCreate runs. Otherwise views that build their content against
	// m_viewFrame would find no frame. The meta path bypasses this CreateView
	// entirely (its own OnCreate asks for the tab itself), so there is no
	// double creation.
	ibClientFrame::CreateChildFrame(view.get());

	if (!view->OnCreate(doc, flags))
		return nullptr;

	// Show the tab now that the view has built its content.
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

ibMetaDocument* ibDocManager::GetCurrentMetaDocument() const
{
	return dynamic_cast<ibMetaDocument*>(GetCurrentDocument());
}

// THE BASE SET: what every application's manager has — the shared templates and the shared schemas. An
// application's own manager adds its own in its ctor (ibDocManagerRuntime, ibDocManagerDesigner).
ibDocManager::ibDocManager(long WXUNUSED(flags), bool initialize)
{
	m_defaultDocumentNameCounter = 1;
	m_currentView = nullptr;
	m_maxDocsOpen = INT_MAX;
	if (initialize)
		Initialize();

	// Text / Spreadsheet / Help — file templates, keyed by no CLSID; the id each is given is its picture's, sent with
	// the template to a client choosing what to create.
	AddDocTemplate(g_metaModuleCLSID,
		_("Text document"), ibTextFileDocument::FileMask(), ibTextFileDocument::FileExtensions(),
		_("Text Doc"), _("Text View"),
		CLASSINFO(ibTextFileDocument), CLASSINFO(ibTextEditView),
		ibTEMPLATE_VISIBLE);

	// EVERY TABLE FORMAT, OURS INCLUDED — asked of the registry (backend/sheetFormat/), not assembled here.
	AddDocTemplate(g_metaTemplateCLSID,
		_("Spreadsheet document"), ibSheetFormatMask(), ibSheetFormatExtensions(),
		_("Spreadsheet Doc"), _("Spreadsheet View"),
		CLASSINFO(ibSpreadsheetFileDocument), CLASSINFO(ibSpreadsheetEditView),
		ibTEMPLATE_VISIBLE);

	AddDocTemplate(g_metaSectionCLSID,
		_("Help document"), wxT("*.hle"), wxT("hle"),
		_("Help Doc"), _("Help View"),
		CLASSINFO(ibHelpFileDocument), CLASSINFO(ibHelpEditView),
		ibTEMPLATE_INVISIBLE);

	// Registration journal — no metaobject. Invisible to File→New; reached through
	// CreateDocument<ibAuditLogDocument>(), which resolves via FindTemplateByDocClassInfo over m_templates.
	AddDocTemplate(g_picUserActiveCLSID,
		_("Registration journal"), wxEmptyString, wxEmptyString,
		_("Audit Log Doc"), _("Audit Log View"),
		CLASSINFO(ibAuditLogDocument), CLASSINFO(ibAuditLogView),
		ibTEMPLATE_INVISIBLE);

	// Home page — the composite tab (N runtime forms in one tab). Same registration shape as the journal: an
	// invisible template reached through CreateDocument<ibHomePageDocument>() from ibHomePageDocument::ShowHomePage,
	// which the client's start calls after the start-up script runs.
	AddDocTemplate(g_picHomePageCLSID,
		_("Home page"), wxEmptyString, wxEmptyString,
		_("Home Page Doc"), _("Home Page View"),
		CLASSINFO(ibHomePageDocument), CLASSINFO(ibHomePageView),
		ibTEMPLATE_INVISIBLE);

	// Active users — the sessions and the locks; the designer's dialog and the runtime's showed the same tables.
	RegisterSchema(ibClientSchemaKind::ActiveUser, std::make_unique<ibSchemaActiveUser>());

	// The commands every document answers to, with the keys the desktop's windows gave them (SetDefaultHotKeys).
	RegisterCommand(ibDocCommand::Undo,  _("Undo"),  wxT("Ctrl+Z"));
	RegisterCommand(ibDocCommand::Redo,  _("Redo"),  wxT("Ctrl+Y"));
	RegisterCommand(ibDocCommand::Save,  _("Save"),  wxT("Ctrl+S"));
	RegisterCommand(ibDocCommand::Close, _("Close"), wxEmptyString);
}

void ibDocManager::RegisterSchema(ibClientSchemaKind kind, std::unique_ptr<ibClientSchema> schema)
{
	m_schemas[kind] = std::move(schema);
}

void ibDocManager::RegisterCommand(ibDocCommand command, const wxString& title, const wxString& shortcut)
{
	m_commands[command] = ibDocCommandInfo{ title, shortcut };
}

const ibClientSchema* ibDocManager::FindSchema(ibClientSchemaKind kind) const
{
	const auto found = m_schemas.find(kind);
	return found != m_schemas.end() ? found->second.get() : nullptr;
}

std::vector<ibClientSchemaKind> ibDocManager::GetSchemaKinds() const
{
	std::vector<ibClientSchemaKind> kinds;
	for (const auto& schema : m_schemas)
		kinds.push_back(schema.first);
	return kinds;
}

// ----------------------------------------------------------------------------
// AddDocTemplate — meta-template registrations (reachable via the
// AssociateTemplate base path, just iterates the same m_templates).
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

	docTemplate->SetPictureID(id);

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
	// Tools / advanced-object templates: never offered for a new document
	// (always INVISIBLE); SAVE_AS_FILE only when the template can produce
	// a stand-alone file.
	auto* docTemplate = new ibMetaDocTemplate(
		this, descr, filter, wxEmptyString, ext,
		wxEmptyString, wxEmptyString,
		docClassInfo, viewClassInfo,
		ibTEMPLATE_INVISIBLE | (!ext.IsEmpty() ? ibTEMPLATE_SAVE_AS_FILE : 0));

	docTemplate->SetClassID(clsid);
	docTemplate->SetPictureID(clsid);   // a class's picture is registered under its clsid

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
// OpenForm — the metadata-driven open entry point.
// ----------------------------------------------------------------------------

ibMetaDocument* ibDocManager::OpenForm(ibValueMetaObject* metaObject, long flags)
{
	// ⭐⭐ ALREADY OPEN? THEN RAISE IT. This opened UNCONDITIONALLY, so the second Open on the same
	// module built a rival document over the first: two editors of one text, each unaware of the
	// other's edits, and whichever was saved last won. It is what made the property panel's Open
	// links look like they "worked once" — they worked every time, and every time made another one.
	//
	// ⚠ THE CHECK BELONGS HERE, in the manager, and not in the tree that used to do this by hand
	// (Max, 2026-09-01: *"before, it all went through the tree; now let it all go through the
	// manager"*). Every road that opens a metaobject arrives at this function — the navigator, a
	// property link, the debugger stopping on a line, a tool — and a check written at one of them
	// is a check the other three do without.
	if (ibMetaDocument* already = FindOpenDocument(metaObject)) {
		already->Activate();
		return already;
	}

	ibMetaDocTemplate* docTemplate = FindMetaTemplate(metaObject->GetClassType());
	if (docTemplate == nullptr)
		return nullptr;

	wxClassInfo* docClassInfo = docTemplate->GetDocClassInfo();
	wxASSERT(docClassInfo);

	auto* newDocument = wxDynamicCast(docClassInfo->CreateObject(), ibMetaDocument);
	wxASSERT(newDocument);

	try {
		newDocument->SetTitle(metaObject->GetModuleName());
		newDocument->SetFilename(metaObject->GetDocPath());
		newDocument->SetDocumentTemplate(docTemplate);
		newDocument->SetMetaObject(metaObject);

		AddDocument(newDocument);

		newDocument->SetIcon(ibBackendPicture::GetServerPicture(metaObject->GetClassType()));

		if (newDocument->OnCreate(metaObject->GetModuleName(), flags | ibDOC_NEW)) {
			newDocument->SetCommandProcessor(newDocument->OnCreateCommandProcessor());
			return newDocument;
		}

		newDocument->DeleteAllViews();
		return nullptr;
	}
	catch (...) {
		ibJournalError(wxT("docview"), wxT("OpenForm: failed to create document view"));
		if (GetDocuments().Member(newDocument))
			newDocument->DeleteAllViews();
	}
	return nullptr;
}

#endif // wxUSE_DOC_VIEW_ARCHITECTURE
