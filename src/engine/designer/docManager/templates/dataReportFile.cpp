#include "dataReportFile.h"

wxIMPLEMENT_DYNAMIC_CLASS(CReportEditView, CMetaView);

bool CReportEditView::OnCreate(CMetaDocument *doc, long flags)
{
	m_metaTree = new CDataReportTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return CMetaView::OnCreate(doc, flags);
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
		SetFrame(nullptr);
	}

	if (CMetaView::OnClose(deleteWindow))
		return m_metaTree->Destroy();

	return false;
}

wxIMPLEMENT_DYNAMIC_CLASS(CReportEditDocument, CMetaDocument);

bool CReportEditDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new CMetaDataReport();
	if (!CMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

#include "frontend/mainFrame/mainFrame.h"

bool CReportEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseConfiguration(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(commonMetaData->GetCommonMetaObject());
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
	return CMetaDocument::IsModified();
}

void CReportEditDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);
}

CDataReportTree *CReportEditDocument::GetMetaTree() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CReportEditView)->GetMetaTree() : nullptr;
}
