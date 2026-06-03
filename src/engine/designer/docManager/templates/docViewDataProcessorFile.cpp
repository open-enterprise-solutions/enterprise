#include "docViewDataProcessorFile.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorEditView, ibMetaView);

bool ibDataProcessorEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_metaTree = new ibDataProcessorTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return ibView::OnCreate(docBase, flags);
}

void ibDataProcessorEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_metaTree->ActivateTree();
}

void ibDataProcessorEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibDataProcessorEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow && GetFrame()) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {
		
		m_metaTree->Freeze();
		
		m_metaTree->Destroy();
		m_metaTree = nullptr;

		return true;
	}

	return false;
}

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorFileDocument, ibMetaDocument);

bool ibDataProcessorFileDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new ibMetaDataDataProcessor();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

#include "frontend/mainFrame/mainFrame.h"

bool ibDataProcessorFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}
	objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibDataProcessorFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool ibDataProcessorFileDocument::DoSaveDocument(const wxString& filename)
{
	if (!GetMetaTree()->Save())
		return false;

	if (!m_metaData->SaveToFile(filename))
		return false;

	return true;
}

bool ibDataProcessorFileDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibDataProcessorFileDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

ibDataProcessorTree* ibDataProcessorFileDocument::GetMetaTree() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibDataProcessorEditView)->GetMetaTree() : nullptr;
}
