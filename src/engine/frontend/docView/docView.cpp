////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"

#include "backend/appData.h"
#include "backend/metaCollection/metaObject.h"

#include "frontend/mainFrame/mainFrame.h"

wxIMPLEMENT_CLASS(ibMetaDocument, wxDocument);

wxIMPLEMENT_CLASS(ibMetaDataDocument, ibMetaDocument);
wxIMPLEMENT_CLASS(ibValueModulibDocument, ibMetaDocument);

wxIMPLEMENT_CLASS(ibMetaView, wxView);

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
	if (ibApplicationData::IsForceExit())
		return false;

	wxScopedPtr<ibMetaView> view(DoCreateView());
	if (!view)
		return false;

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

	view->SetDocument(this);

	ibFrontendDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);

	if (!view->OnCreate(this, flags))
		return false;

	view->ShowFrame();
	return view.release() != nullptr;
}

bool ibMetaDocument::OnSaveModified()
{
	if (ibApplicationData::IsForceExit())
		return true;

	if (m_metaObject != nullptr)
		return true;

	return wxDocument::OnSaveModified();
}

bool ibMetaDocument::OnSaveDocument(const wxString& filename)
{
	if (ibApplicationData::IsForceExit())
		return false;

	if (m_metaObject != nullptr)
		return true;
	
	return wxDocument::OnSaveDocument(filename);
}

#include "docManager.h"
#include "backend/metadataConfiguration.h"

bool ibMetaDocument::OnCloseDocument()
{
	if (m_documentParent != nullptr) {
		docManager->RemoveDocument(this);
	}

	ibBackendMetadataTree* metaTree =
		m_metaObject != nullptr ? m_metaObject->GetMetaDataTree() : nullptr;

	if (metaTree == nullptr)
		objectInspector->SelectObject(nullptr);
	else
		metaTree->Activate();

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
	if (!ibApplicationData::IsForceExit()) {
		
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
	if (!ibApplicationData::IsForceExit()) {

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
	if (!ibApplicationData::IsForceExit()) {
	
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