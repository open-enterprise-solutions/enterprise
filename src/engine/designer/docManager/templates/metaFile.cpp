#include "metaFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CMetadataView, CMetaView);

bool CMetadataView::OnCreate(CMetaDocument* doc, long flags)
{
	m_metaTree = new CMetadataTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(true);

	return CMetaView::OnCreate(doc, flags);
}

void CMetadataView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, CMetadataTree draws itself
}

bool CMetadataView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (CMetaView::OnClose(deleteWindow)) {
		m_metaTree->Freeze();
		return m_metaTree->Destroy();
	}
	
	return false;
}

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CMetadataDocument, CMetaDocument);

bool CMetadataDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new CMetaDataConfigurationFile(true);

	if (!CMetaDocument::OnCreate(path, flags))
		return false;
	
	return true; 
}

#include "frontend/mainFrame/mainFrame.h"

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CMetadataDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return m_metaData->RunConfiguration(onlyLoadFlag);
}

bool CMetadataDocument::OnCloseDocument()
{
	if (!m_metaData->CloseConfiguration(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(commonMetaData->GetCommonMetaObject());
	return true;
}

bool CMetadataDocument::DoSaveDocument(const wxString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

	return true;
}

bool CMetadataDocument::IsModified() const
{
	return false;
}

void CMetadataDocument::Modify(bool modified)
{
}

// ----------------------------------------------------------------------------
// CTextEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CMetataEditDocument, CMetadataDocument);

CMetadataTree* CMetataEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CMetadataView)->GetMetaTree() : nullptr;
}
