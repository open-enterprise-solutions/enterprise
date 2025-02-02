#include "dataProcessorFile.h"

// ----------------------------------------------------------------------------
// CTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorView, CMetaView);

bool CDataProcessorView::OnCreate(CMetaDocument *doc, long flags)
{
	return CMetaView::OnCreate(doc, flags);
}

void CDataProcessorView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool CDataProcessorView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	return CMetaView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// CTextDocument: CDataProcessorDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorDocument, CMetaDocument);
\
bool CDataProcessorDocument::OnCreate(const wxString& path, long flags)
{
	/*if (!CMetaDocument::OnCreate(path, flags))
		return false;*/
	m_metaData = new CMetaDataDataProcessor();
	return true;
}

#include "frontend/mainFrame/objinspect/objinspect.h"

bool CDataProcessorDocument::OnCloseDocument()
{
	if (!m_metaData->CloseConfiguration(forceCloseFlag)) {
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
	return CMetaDocument::IsModified();
}

void CDataProcessorDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);
}