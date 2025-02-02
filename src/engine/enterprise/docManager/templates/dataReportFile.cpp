#include "dataReportFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CReportView, CMetaView);

bool CReportView::OnCreate(CMetaDocument *doc, long flags)
{
	return CMetaView::OnCreate(doc, flags);
}

void CReportView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CReportView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow && GetFrame()) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	return CMetaView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CReportDocument, CMetaDocument);

bool CReportDocument::OnCreate(const wxString& path, long flags)
{
	/*if (!CMetaDocument::OnCreate(path, flags))
		return false;*/

	m_metaData = new CMetaDataReport();
	return true;
}

#include "frontend/mainFrame/objinspect/objinspect.h"

bool CReportDocument::OnCloseDocument()
{
	if (!m_metaData->CloseConfiguration(forceCloseFlag)) {
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
	return CMetaDocument::IsModified();
}

void CReportDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);
}
