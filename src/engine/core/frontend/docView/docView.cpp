////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"
#include "appData.h"
#include "frontend/mainFrame.h"

wxIMPLEMENT_CLASS(CDocument, wxDocument);
wxIMPLEMENT_CLASS(IMetaDataDocument, CDocument);
wxIMPLEMENT_CLASS(IModuleDocument, CDocument);

wxIMPLEMENT_CLASS(CView, wxView);

//******************************************************************************
//*                            Document implementation                         *
//******************************************************************************

#include "core/metadata/metaObjectsDefines.h"

CView* CDocument::DoCreateView()
{
	wxClassInfo* viewClassInfo = m_documentTemplate->GetViewClassInfo();
	if (viewClassInfo == NULL)
		return NULL;

	return static_cast<CView*>(viewClassInfo->CreateObject());
}

wxString CDocument::GetModuleName() const
{
	return m_metaObject->GetFullName();
}

CDocument::CDocument(CDocument* docParent) :
	wxDocument(), m_metaObject(NULL), m_childDoc(true)
{
	m_docParent = docParent;
	
	if (docParent != NULL) {
		docParent->m_childDocs.push_back(this);
	}
	
	m_documentModified = false;
}

CDocument::~CDocument()
{
	if (m_docParent != NULL) {
		m_docParent->m_childDocs.remove(this);
	}
}

bool CDocument::OnCreate(const wxString& path, long flags)
{
	wxScopedPtr<CView> view(DoCreateView());
	if (!view)
		return false;

	view->SetDocument(this);

	wxAuiDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);

	if (!view->OnCreate(this, flags))
		return false;

	view->ShowFrame();
	return view.release() != NULL;
}

bool CDocument::OnSaveModified()
{
	if (m_metaObject == NULL) {
		return wxDocument::OnSaveModified();
	}

	return true;
}

bool CDocument::OnSaveDocument(const wxString& filename)
{
	if (m_metaObject)
		return true;

	return wxDocument::OnSaveDocument(filename);
}

#include "docManager.h"
#include "frontend/metatree/metaTreeWnd.h"

bool CDocument::OnCloseDocument()
{
	if (m_docParent != NULL) {
		docManager->RemoveDocument(this);
	}

	if (appData->DesignerMode()) {
		if (m_metaObject) {
			IMetadata* metaData = m_metaObject->GetMetadata();
			wxASSERT(metaData);
			IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->OnCloseDocument(this);
		}
	}

	// Tell all views that we're about to close
	NotifyClosing();
	DeleteContents();
	return true;
}

bool CDocument::IsModified() const
{
	if (m_metaObject != NULL) {
		IMetadata* metaData = m_metaObject->GetMetadata();
		if (metaData) {
			return metaData->IsModified();
		}
	}

	return wxDocument::IsModified();
}

void CDocument::Modify(bool modify)
{
	if (m_metaObject != NULL) {
		IMetadata* metaData = m_metaObject->GetMetadata();
		if (metaData) {
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

bool CDocument::Save()
{
	if (AlreadySaved())
		return true;

	if (m_docParent != NULL &&
		!m_docParent->Save()) {
		return false;
	}

	if (m_docParent == NULL && IsChildDocument() &&
		m_metaObject != NULL) {
		if (metadata->SaveMetadata()) {
			return false;
		}
	}

	if (m_documentFile.IsEmpty() ||
		!m_savedYet) {
		return SaveAs();
	}

	return OnSaveDocument(m_documentFile);
}

bool CDocument::SaveAs()
{
	if (m_metaObject)
		return true;

	return wxDocument::SaveAs();
}

bool CDocument::Close()
{
	if (!OnSaveModified())
		return false;

	// When the parent document closes, its children must be closed as well as
	// they can't exist without the parent.

	// As usual, first check if all children can be closed.
	wxDList<CDocument>::const_iterator it = m_childDocs.begin();
	for (wxDList<CDocument>::const_iterator end = m_childDocs.end(); it != end; ++it)
	{
		if (!(*it)->OnSaveModified()) {
			// Leave the parent document opened if a child can't close.
			return false;
		}
	}

	// Now that they all did, do close them: as m_childDocs is modified as
	// we iterate over it, don't use the usual for-style iteration here.
	while (!m_childDocs.empty())
	{
		CDocument* const childDoc = m_childDocs.front();

		// This will call OnSaveModified() once again but it shouldn't do
		// anything as the document was just saved or marked as not needing to
		// be saved by the call to OnSaveModified() that returned true above.
		if (!childDoc->Close()) {
			return false;
		}

		// Delete the child document by deleting all its views.
		childDoc->DeleteAllViews();
	}

	return OnCloseDocument();
}

void CDocument::UpdateAllViews(wxView* sender, wxObject* hint)
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

bool CDocument::DeleteAllViews()
{
	wxDocManager* manager = GetDocumentManager();

	// first check if all views agree to be closed
	const wxList::iterator end = m_documentViews.end();
	for (wxList::iterator i = m_documentViews.begin(); i != end; ++i)
	{
		wxView* view = (wxView*)*i;
		if (!view->Close())
			return false;
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

#include "frontend/codeEditor/codeEditorCtrl.h"

 void IModuleDocument::SetCurrentLine(int lineBreakpoint, bool setBreakpoint) {
	CCodeEditorCtrl* codeEditor = GetCodeEditor();
	wxASSERT(codeEditor);
	if (codeEditor != NULL)
		codeEditor->SetCurrentLine(lineBreakpoint, setBreakpoint);
}

void IModuleDocument::SetToolTip(const wxString& resultStr) {
	CCodeEditorCtrl* codeEditor = GetCodeEditor();
	wxASSERT(codeEditor);
	if (codeEditor != NULL)
		codeEditor->SetToolTip(resultStr);
}