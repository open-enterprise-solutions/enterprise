#include "dataProcessorFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorView, CView);

bool CDataProcessorView::OnCreate(CDocument *doc, long flags)
{
	return CView::OnCreate(doc, flags);
}

void CDataProcessorView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CDataProcessorView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}


wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorEditView, CView);

bool CDataProcessorEditView::OnCreate(CDocument *doc, long flags)
{
	m_metaTree = new CDataProcessorTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return CView::OnCreate(doc, flags);
}

void CDataProcessorEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CDataProcessorEditView::OnClose(bool deleteWindow)
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

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorDocument, CDocument);

bool CDataProcessorDocument::OnCreate(const wxString& path, long flags)
{
	/*if (!CDocument::OnCreate(path, flags))
		return false;*/
	m_metaData = new CMetadataDataProcessor();
	return true;
}

#include "frontend/objinspect/objinspect.h"

bool CDataProcessorDocument::OnCloseDocument()
{
	if (!m_metaData->CloseMetadata(forceCloseFlag)) {
		return false;
	}

	return wxDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CDataProcessorDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;
	
	//We must delete document after initialization
	return false;
}

bool CDataProcessorDocument::DoSaveDocument(const wxString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

	return true;
}

bool CDataProcessorDocument::IsModified() const
{
	return CDocument::IsModified();
}

void CDataProcessorDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorEditDocument, CDocument);

bool CDataProcessorEditDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new CMetadataDataProcessor();
	if (!CDocument::OnCreate(path, flags))
		return false;
	return true;
}

#include "frontend/objinspect/objinspect.h"

bool CDataProcessorEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseMetadata(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(metadata->GetCommonMetaObject());
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
	return CDocument::IsModified();
}

void CDataProcessorEditDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

CDataProcessorTree *CDataProcessorEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CDataProcessorEditView)->GetMetaTree() : NULL;
}
