#include "dataProcessorFile.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorEditView, CMetaView);

bool CDataProcessorEditView::OnCreate(CMetaDocument* doc, long flags)
{
	m_metaTree = new CDataProcessorTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return CMetaView::OnCreate(doc, flags);
}

void CDataProcessorEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CDataProcessorEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow && GetFrame()) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (CMetaView::OnClose(deleteWindow))
		return m_metaTree->Destroy();

	return false;
}

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorEditDocument, CMetaDocument);

bool CDataProcessorEditDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new CMetaDataDataProcessor();
	if (!CMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

#include "frontend/mainFrame/mainFrame.h"

bool CDataProcessorEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseConfiguration(forceCloseFlag)) {
		return false;
	}
	objectInspector->SelectObject(commonMetaData->GetCommonMetaObject());
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CDataProcessorEditDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool CDataProcessorEditDocument::DoSaveDocument(const wxString& filename)
{
	if (!GetMetaTree()->Save())
		return false;

	if (!m_metaData->SaveToFile(filename))
		return false;

	return true;
}

bool CDataProcessorEditDocument::IsModified() const
{
	return CMetaDocument::IsModified();
}

void CDataProcessorEditDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);
}

CDataProcessorTree* CDataProcessorEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CDataProcessorEditView)->GetMetaTree() : nullptr;
}
