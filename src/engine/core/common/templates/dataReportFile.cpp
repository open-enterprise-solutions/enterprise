#include "dataReportFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CReportView, CView);

bool CReportView::OnCreate(CDocument *doc, long flags)
{
	return CView::OnCreate(doc, flags);
}

void CReportView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CReportView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}


wxIMPLEMENT_DYNAMIC_CLASS(CReportEditView, CView);

bool CReportEditView::OnCreate(CDocument *doc, long flags)
{
	m_metaTree = new CDataReportTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return CView::OnCreate(doc, flags);
}

void CReportEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CReportEditView::OnClose(bool deleteWindow)
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

wxIMPLEMENT_DYNAMIC_CLASS(CReportDocument, CDocument);

bool CReportDocument::OnCreate(const wxString& path, long flags)
{
	/*if (!CDocument::OnCreate(path, flags))
		return false;*/

	m_metaData = new CMetadataReport();
	return true;
}

#include "frontend/objinspect/objinspect.h"

bool CReportDocument::OnCloseDocument()
{
	if (!m_metaData->CloseMetadata(forceCloseFlag)) {
		return false;
	}

	return wxDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CReportDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	//We must delete document after initialization
	return false;
}

bool CReportDocument::DoSaveDocument(const wxString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

	return true;
}

bool CReportDocument::IsModified() const
{
	return CDocument::IsModified();
}

void CReportDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

wxIMPLEMENT_DYNAMIC_CLASS(CReportEditDocument, CDocument);

bool CReportEditDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	m_metaData = new CMetadataReport();
	return true;
}

#include "frontend/objinspect/objinspect.h"

bool CReportEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseMetadata(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(metadata->GetCommonMetaObject());
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CReportEditDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool CReportEditDocument::DoSaveDocument(const wxString& filename)
{
	if (!GetMetaTree()->Save())
		return false;

	if (!m_metaData->SaveToFile(filename))
		return false;

	return true;
}

bool CReportEditDocument::IsModified() const
{
	return CDocument::IsModified();
}

void CReportEditDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

CDataReportTree *CReportEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CReportEditView)->GetMetaTree() : NULL;
}
