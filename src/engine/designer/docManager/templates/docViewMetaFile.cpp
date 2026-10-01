#include "docViewMetaFile.h"

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataEditView, ibMetaView);

bool ibMetadataEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_metaTree = new ibConfigurationTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(true);

	return ibView::OnCreate(docBase, flags);
}

void ibMetadataEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_metaTree->ActivateTree();
}

void ibMetadataEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, ibConfigurationTree draws itself
}

#include "docManager/docManager.h"

bool ibMetadataEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
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

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include "frontend/mainFrame/mainFrame.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataBrowserDocument, ibMetaDocument);

bool ibMetadataBrowserDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	
	return GetMetaTree()->Load(m_metaData);
}

bool ibMetadataBrowserDocument::OnCloseDocument()
{
	objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
	return true;
}

// ----------------------------------------------------------------------------

ibConfigurationTree* ibMetadataBrowserDocument::GetMetaTree() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibMetadataEditView)->GetMetaTree() : nullptr;
}

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataFileDocument, ibMetadataBrowserDocument);

bool ibMetadataFileDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new ibMetaDataConfigurationFile();

	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibMetadataFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadConfigFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return m_metaData->RunDatabase(onlyLoadFlag);
}

bool ibMetadataFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
	return true;
}

bool ibMetadataFileDocument::DoSaveDocument(const wxString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

	return true;
}

bool ibMetadataFileDocument::IsModified() const
{
	return false;
}

void ibMetadataFileDocument::Modify(bool modified)
{
}