#include "metaFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CMetadataView, CView);

bool CMetadataView::OnCreate(CDocument *doc, long flags)
{
	m_metaTree = new CMetadataTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(true);

	return CView::OnCreate(doc, flags);
}

void CMetadataView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, CMetadataTree draws itself
}

bool CMetadataView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CMetadataDocument, CDocument);

bool CMetadataDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	m_metaData = new CConfigFileMetadata(true);
	return m_metaData->RunMetadata(onlyLoadFlag);
}

#include "frontend/objinspect/objinspect.h"

bool CMetadataDocument::OnCloseDocument()
{
	if (!m_metaData->CloseMetadata(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(metadata->GetCommonMetaObject());
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CMetadataDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

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

CMetadataTree * CMetataEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CMetadataView)->GetMetaTree() : NULL;
}
