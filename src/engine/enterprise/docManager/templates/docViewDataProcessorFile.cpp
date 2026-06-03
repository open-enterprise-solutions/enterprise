#include "docViewDataProcessorFile.h"

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorEditView, ibMetaView);

bool ibDataProcessorEditView::OnCreate(ibDocument *doc, long flags)
{
	return ibView::OnCreate(doc, flags);
}

void ibDataProcessorEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibDataProcessorEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	return ibMetaView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// ibTextDocument: ibDataProcessorFileDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorFileDocument, ibMetaDocument);
\
bool ibDataProcessorFileDocument::OnCreate(const wxString& path, long flags)
{
	/*if (!ibMetaDocument::OnCreate(path, flags))
		return false;*/
	m_metaData = new ibMetaDataDataProcessor();
	return true;
}

#include "frontend/mainFrame/objinspect/objinspect.h"

bool ibDataProcessorFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}

	return ibDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibDataProcessorFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;
	
	//We must delete document after initialization
	return false;
}

bool ibDataProcessorFileDocument::DoSaveDocument(const wxString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

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