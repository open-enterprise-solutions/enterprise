////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "backend/session/session.h"
#include "backend/metaCollection/metaObject.h"

#ifdef OES_USE_WEB
#include "frontend/web/webFrame.h"
#include "frontend/web/webChildFrame.h"
#include "frontend/web/webWindow.h"
#include "frontend/visualView/visualHostClient.h"
#else
#include "frontend/mainFrame/mainFrame.h"
#endif

wxIMPLEMENT_CLASS(ibMetaDocument, wxDocument);

wxIMPLEMENT_CLASS(ibMetaDataDocument, ibMetaDocument);
wxIMPLEMENT_CLASS(ibValueModulibDocument, ibMetaDocument);

wxIMPLEMENT_CLASS(ibMetaView, wxView);

bool ibMetaView::ShowFrame(bool show)
{
#ifdef OES_USE_WEB
	// Web: flip the owning ibWebDocChildFrame's shown flag via the
	// m_webFrame back-pointer set by ibWebFrame::CreateChildFrame.
	// Mirrors desktop's "reveal the wxDocChildFrame" semantics — the
	// single "make it visible now" trigger.
	if (m_webFrame != nullptr) {
		m_webFrame->Show(show);
		return true;
	}
#endif
	if (m_viewFrame != nullptr && m_viewFrame->Show(show))
		return true;
	return false;
}

//******************************************************************************
//*                            Document implementation                         *
//******************************************************************************

ibMetaView* ibMetaDocument::DoCreateView()
{
	wxClassInfo* viewClassInfo = m_documentTemplate->GetViewClassInfo();
	if (viewClassInfo == nullptr)
		return nullptr;
	return static_cast<ibMetaView*>(viewClassInfo->CreateObject());
}

wxString ibMetaDocument::GetModuleName() const
{
	if (m_metaObject)
		return m_metaObject->GetFullName();
	return wxString();
}

ibMetaDocument::ibMetaDocument(ibMetaDocument* docParent) :
	wxDocument(), m_metaObject(nullptr), m_childDoc(true)
{
	m_documentParent = docParent;

	if (docParent != nullptr)
		docParent->m_childDocs.push_back(this);

	m_documentModified = false;
}

ibMetaDocument::~ibMetaDocument()
{
	if (m_documentParent != nullptr)
		m_documentParent->m_childDocs.remove(this);
}

#include <wx/scopedptr.h>

bool ibMetaDocument::OnCreate(const wxString& path, long flags)
{
	if (ibSession::IsCurrentForceExit())
		return false;

	wxScopedPtr<ibMetaView> view(DoCreateView());
	if (!view)
		return false;

	view->SetDocument(this);

	// Shared doc/view pipeline: spawn the child-frame for this view.
	// Desktop hits ibFrontendDocMDIFrame (CAuiDocChildFrame inside an
	// AUI MDI parent); web hits ibWebFrame (ibWebDocChildFrame parked
	// in the session's m_tabs). Both sides have matching static factory
	// signatures so only the class-qualifier differs.
#ifdef OES_USE_WEB
	ibFrontendWindow* childFrame =
		ibWebFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, 0);
#else
	bool createModal = false;
	for (wxWindow* window : wxTopLevelWindows) {
		if (window->IsKindOf(CLASSINFO(wxDialog))) {
			if (((wxDialog*)window)->IsModal()) {
				createModal = true; break;
			}
		}
	}

	long style = wxDEFAULT_FRAME_STYLE;
	if (createModal) style = style | wxCREATE_SDI_FRAME;

	ibFrontendDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);
#endif

	if (!view->OnCreate(this, flags))
		return false;

#ifdef OES_USE_WEB
	// Web: view's OnCreate built the ibVisualHostClient; wire it into
	// the tab's ibWebWindow subtree so JSON serialization finds it.
	// Host ownership stays with the view (m_visualHost, deleted by
	// its dtor); this is a non-owning parent edge. ~ibWebDocChildFrame
	// detaches the host before the view dies to avoid a dangling
	// m_children walk in the base ~ibWebWindow.
	if (auto* editView = dynamic_cast<ibFormVisualEditView*>(view.get())) {
		if (auto* host = editView->GetVisualHost()) {
			if (auto* tab = dynamic_cast<ibWebDocChildFrame*>(childFrame)) {
				host->SetParent(tab);
			}
		}
	}
#endif
	// Unified ShowFrame — the explicit "make it visible" trigger.
	// Desktop: reveals the CAuiDocChildFrame. Web: base is a no-op
	// (m_viewFrame is null), subclasses may override to activate
	// the owning tab.
	view->ShowFrame();
	return view.release() != nullptr;
}

bool ibMetaDocument::OnSaveModified()
{
	if (ibSession::IsCurrentForceExit())
		return true;

	if (m_metaObject != nullptr)
		return true;

	return wxDocument::OnSaveModified();
}

bool ibMetaDocument::OnSaveDocument(const wxString& filename)
{
	if (ibSession::IsCurrentForceExit())
		return false;

	bool ok = false;
	if (m_metaObject != nullptr)
		ok = true;
	else
		ok = wxDocument::OnSaveDocument(filename);

	// Fan out to plugin subscribers. Payload stays null for now — the
	// document filename + metaobject id may be added when the ABI grows
	// a structured-payload type.
	if (ok) {
		if (auto* pm = appData->GetPluginManager())
			pm->FireEvent(wxT("DocumentSaved"));
	}
	return ok;
}

#include "docManager.h"
#include "backend/metadataConfiguration.h"

bool ibMetaDocument::OnCloseDocument()
{
#ifndef OES_USE_WEB
	// docManager is the desktop-wide ibMetaDocManager singleton; on web
	// there's no such global (wfrontend.dll doesn't construct one —
	// see docManager.cpp guards), and the session's tab list owns the
	// docs directly, so RemoveDocument has no counterpart here.
	if (m_documentParent != nullptr) {
		docManager->RemoveDocument(this);
	}
#endif

	ibBackendMetadataTree* metaTree =
		m_metaObject != nullptr ? m_metaObject->GetMetaDataTree() : nullptr;

#ifndef OES_USE_WEB
	// objectInspector is the designer's property panel (not in wfrontend.dll)
	// and Activate() would bring its tree-ctrl into focus. Neither is
	// meaningful on web, so the selection/activation side-effect is skipped.
	if (metaTree == nullptr)
		objectInspector->SelectObject(nullptr);
	else
		metaTree->Activate();
#else
	(void)metaTree;
#endif

	// Tell all views that we're about to close
	NotifyClosing();
	DeleteContents();
	return true;
}

bool ibMetaDocument::IsModified() const
{
	if (m_metaObject != nullptr)
		return false;
	return wxDocument::IsModified();
}

void ibMetaDocument::Modify(bool modify)
{
	if (!ibSession::IsCurrentForceExit()) {
		
		if (m_metaObject != nullptr) {
			ibMetaData* metaData = m_metaObject->GetMetaData();
			if (metaData != nullptr) {
				metaData->Modify(modify);
			}
		}
		else if (modify != m_documentModified) {
			m_documentModified = modify;
			// Allow views to append asterix to the title
			wxView* view = GetFirstView();
			if (view) {
				view->OnChangeFilename();
			}
		}
	}
}

bool ibMetaDocument::Save()
{
	if (!ibSession::IsCurrentForceExit()) {

		if (AlreadySaved())
			return true;

		if (m_documentParent != nullptr &&
			!m_documentParent->Save()) {
			return false;
		}

		if ((m_documentParent == nullptr && m_metaObject != nullptr) && IsChildDocument()) {
			if (activeMetaData->SaveDatabase()) return false;
		}

		if (m_documentFile.IsEmpty() ||
			!m_savedYet) {
			return SaveAs();
		}

		return OnSaveDocument(m_documentFile);
	}

	return false; 
}

bool ibMetaDocument::SaveAs()
{
	if (!ibSession::IsCurrentForceExit()) {
	
		if (m_metaObject != nullptr)
			return true;

		return wxDocument::SaveAs();
	}

	return false;
}

#include <wx/dlist.h>

bool ibMetaDocument::Close()
{
	if (!OnSaveModified())
		return false;

	// When the parent document closes, its children must be closed as well as
	// they can't exist without the parent.
	ibMetaDocument const* documentParent = m_documentParent;

	// As usual, first check if all children can be closed.
	wxDList<ibMetaDocument>::const_iterator it = m_childDocs.begin();
	for (wxDList<ibMetaDocument>::const_iterator end = m_childDocs.end(); it != end; ++it) {
		if ((*it)->IsCloseOnOwnerClose()) {
			if (!(*it)->OnSaveModified()) {
				// Leave the parent document opened if a child can't close.
				return false;
			}
		}
	}

	wxDocManager* documentManager = GetDocumentManager();

	// Now that they all did, do close them: as m_childDocs is modified as
	// we iterate over it, don't use the usual for-style iteration here.
	while (!m_childDocs.empty()) {
		ibMetaDocument* const childDoc = m_childDocs.front();
		if (childDoc->IsCloseOnOwnerClose()) {

			// This will call OnSaveModified() once again but it shouldn't do
			// anything as the document was just saved or marked as not needing to
			// be saved by the call to OnSaveModified() that returned true above.
			if (!childDoc->Close()) {
				return false;
			}

			// Delete the child document by deleting all its views.
			childDoc->DeleteAllViews();
		}
		else {
			if (documentManager != nullptr) {
				childDoc->SetDocParent(nullptr);
				documentManager->AddDocument(childDoc);
			}
		}
	}

	return OnCloseDocument();
}

void ibMetaDocument::UpdateAllViews(wxView* sender, wxObject* hint)
{
	wxList::compatibility_iterator node = m_documentViews.GetFirst();
	while (node) {
		wxView* view = (wxView*)node->GetData();
		if (view != sender)
			view->OnUpdate(sender, hint);
		node = node->GetNext();
	}

	for (auto childDoc : m_childDocs) {
		childDoc->UpdateAllViews(sender, hint);
	}
}

bool ibMetaDocument::DeleteAllViews()
{
	wxDocManager* manager = GetDocumentManager();

	// first check if all views agree to be closed
	const wxList::iterator end = m_documentViews.end();
	for (wxList::iterator i = m_documentViews.begin(); i != end; ++i)
	{
		wxView* view = (wxView*)*i;
		if (!view->Close(false))
			return false;

		view->Activate(false);
	}

	// all views agreed to close, now do close them
	if (m_documentViews.empty()) {
		// normally the document would be implicitly deleted when the last view
		// is, but if don't have any views, do it here instead
		if (manager && manager->GetDocuments().Member(this)) {
			delete this;
		}
	}
	else // have views
	{
		// as we delete elements we iterate over, don't use the usual "from
		// begin to end" loop
		for (;; ) {
			wxView* view = (wxView*)*m_documentViews.begin();

			bool isLastOne = m_documentViews.size() == 1;

			// this always deletes the node implicitly and if this is the last
			// view also deletes this object itself (also implicitly, great),
			// so we can't test for m_documentViews.empty() after calling this!

			delete view;

			if (isLastOne)
				break;
		}
	}

	return true;
}