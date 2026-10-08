////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : OES doc-manager wiring — bodies that the collapsed
//	              ibFrontendDocManager class (declared in docView.h) needs on top of
//	              the wx-fork base in docView.cpp. The file lives in both
//	              frontend (desktop) and wfrontend (web) builds; the few
//	              desktop-specific bodies (Find/Replace dialog parented on
//	              the AUI mainFrame) are reduced to stubs under OES_USE_WEB.
////////////////////////////////////////////////////////////////////////////

#include "frmclient/docView/docManager.h"  // also brings docView.h transitively

#include "frmclient/win/dlgs/choiceTemplate.h"
#include "frmclient/diagnostics/journal.h"

// Desktop-only: Find/Replace dialog parents the wxFindReplaceDialog against
// the main frame singleton. mainFrame.h pulls in ibFrontendMainFrame
// + AUI + objectInspector — none of which exist on wfrontend.dll. Also the
// home of ibFrontendMainFrame::CreateChildFrame used below to wire
// m_viewFrame on every view going through the template path.
#include "frmclient/mainFrame/mainFrame.h"

#if wxUSE_DOC_VIEW_ARCHITECTURE

#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/scopeguard.h>
#include <memory>

wxIMPLEMENT_ABSTRACT_CLASS(ibFrontendDocTemplate, wxObject);

namespace
{

// Extracted from wx/src/common/docview.cpp's anonymous-namespace helper; used
// by ibFrontendDocTemplate::FileMatchesTemplate. Kept private to this TU so it
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
// ibFrontendDocTemplate (wx-fork base)
// ----------------------------------------------------------------------------

ibFrontendDocTemplate::ibFrontendDocTemplate(ibFrontendDocManager *manager,
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

ibFrontendDocTemplate::~ibFrontendDocTemplate()
{
	m_documentManager->DisassociateTemplate(this);
}

// Tries to dynamically construct an object of the right class.
ibFrontendDocument *ibFrontendDocTemplate::CreateDocument(const wxString& path, long flags)
{
	// InitDocument() is supposed to delete the document object if its
	// initialization fails so don't use unique_ptr<> here: this is fragile
	// but unavoidable because the default implementation uses CreateView()
	// which may -- or not -- create a ibFrontendView and if it does create it and its
	// initialization fails then the view destructor will delete the document
	// (via RemoveView()) and as we can't distinguish between the two cases we
	// just have to assume that it always deletes it in case of failure
	ibFrontendDocument * const doc = DoCreateDocument();

	return doc && InitDocument(doc, path, flags) ? doc : nullptr;
}

bool
ibFrontendDocTemplate::InitDocument(ibFrontendDocument* doc, const wxString& path, long flags)
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

	// The command processor after the views, as the desktop's ibMetaDocTemplate did: a view's own undo
	// (the text editor's) needs the view built.
	if (!doc->OnCreate(path, flags))
		return false;

	doc->SetCommandProcessor(doc->OnCreateCommandProcessor());
	guard.Dismiss();

	return true;
}

ibFrontendView *ibFrontendDocTemplate::CreateView(ibFrontendDocument *doc, long flags)
{
	std::unique_ptr<ibFrontendView> view(DoCreateView());
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

	ibFrontendMainFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);

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
bool ibFrontendDocTemplate::FileMatchesTemplate(const wxString& path)
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

ibFrontendDocument *ibFrontendDocTemplate::DoCreateDocument()
{
	if (!m_docClassInfo)
		return nullptr;

	return static_cast<ibFrontendDocument *>(m_docClassInfo->CreateObject());
}

ibFrontendView *ibFrontendDocTemplate::DoCreateView()
{
	if (!m_viewClassInfo)
		return nullptr;

	return static_cast<ibFrontendView *>(m_viewClassInfo->CreateObject());
}

ibDocTemplateVector ibFrontendDocManager::GetTemplatesVector() const
{
	return m_templates.AsVector<ibFrontendDocTemplate*>();
}

// ----------------------------------------------------------------------------
// ibFrontendDocManager — OES extensions
// ----------------------------------------------------------------------------

void ibFrontendDocManager::OnFindDialog(wxCommandEvent& WXUNUSED(event))
{
	if (m_findDialog == nullptr) {
		m_findDialog = new wxFindReplaceDialog(mainFrame, &m_findData, _("Find"));
		m_findDialog->Centre(wxCENTRE_ON_SCREEN | wxBOTH);

		m_findDialog->Bind(wxEVT_FIND,       &ibFrontendDocManager::OnFind,      this);
		m_findDialog->Bind(wxEVT_FIND_NEXT,  &ibFrontendDocManager::OnFind,      this);
		m_findDialog->Bind(wxEVT_FIND_CLOSE, &ibFrontendDocManager::OnFindClose, this);
	}

	m_findDialog->Show(true);
}

void ibFrontendDocManager::OnFindClose(wxFindDialogEvent& WXUNUSED(event))
{
	m_findDialog->Unbind(wxEVT_FIND,       &ibFrontendDocManager::OnFind,      this);
	m_findDialog->Unbind(wxEVT_FIND_NEXT,  &ibFrontendDocManager::OnFind,      this);
	m_findDialog->Unbind(wxEVT_FIND_CLOSE, &ibFrontendDocManager::OnFindClose, this);

	m_findDialog->Destroy();
	m_findDialog = nullptr;
}

void ibFrontendDocManager::OnFind(wxFindDialogEvent& event)
{
	ibFrontendDocument* currDocument = GetCurrentDocument();
	if (currDocument != nullptr) {
		ibFrontendView* firstView = currDocument->GetFirstView();
		if (firstView != nullptr) {
			event.StopPropagation();
			event.SetClientData(m_findDialog);
			firstView->ProcessEvent(event);
		}
	}
}

ibFrontendDocTemplate* ibFrontendDocManager::FindTemplateByDocClassInfo(const wxClassInfo* classInfo) const
{
	for (wxList::compatibility_iterator node = m_templates.GetFirst();
	     node != nullptr; node = node->GetNext())
	{
		auto* t = static_cast<ibFrontendDocTemplate*>(node->GetData());
		if (t != nullptr && t->GetDocClassInfo() == classInfo)
			return t;
	}
	return nullptr;
}

// ibFrontendView::Activate lives here (not in docView.cpp) because it reaches the
// desktop main frame (mainFrame / ibFrontendMainFrame), whose headers are
// only pulled in on this side — same split as the rest of the metadata-aware
// wiring. Lifted up from ibMetaView so the form view (a plain ibFrontendView since the
// doc/view fork) also clears its activation on the main frame; otherwise the
// reduced ibFrontendView path skipped mainFrame->ActivateView and deactivation stuck.
void ibFrontendView::Activate(bool activate)
{
	// Local name shadows the `docManager` macro deliberately — picks the
	// view's owning manager first, falls back to the singleton.
	ibFrontendDocManager* const mgr = m_viewDocument != nullptr ?
		m_viewDocument->GetDocumentManager() : ibFrontendDocManager::GetDocumentManager();

	if (mgr != nullptr && ibFrontendMainFrame::GetFrame()) {
		mainFrame->ActivateView(this, activate);
		OnActivateView(activate, this, mgr->GetCurrentView());
		mgr->ActivateView(this, activate);
	}

	if (activate) ibClientJournalInfo(wxT("docview"), "! <debug> activate view %s", GetViewName());
	else ibClientJournalInfo(wxT("docview"), "! <debug> deactivate view %s", GetViewName());
}

#endif // wxUSE_DOC_VIEW_ARCHITECTURE
