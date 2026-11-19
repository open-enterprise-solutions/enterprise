#include "docViewDataToolFile.h"

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

bool ibDataToolEditView::OnCreate(ibDocument *doc, long flags)
{
	return ibView::OnCreate(doc, flags);
}

void ibDataToolEditView::OnDraw(ibDataNode& /*frame*/)
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibDataToolEditView::OnClose(bool deleteWindow)
{
	// The frame is a client's tab: closing the view closes it (ibView::OnClose).
	return ibMetaView::OnClose(deleteWindow);
}

// ----------------------------------------------------------------------------
// ibTextDocument: ibDataToolFileDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

\
bool ibDataToolFileDocument::OnCreate(const ibString& path, long flags)
{
	/*if (!ibMetaDocument::OnCreate(path, flags))
		return false;*/
	m_metaData = ibMetaData::MakeShared<ibMetaDataDataTool>();
	return true;
}


bool ibDataToolFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(ibRuntimeRequest(forceCloseFlag))) {
		return false;
	}

	return ibDocument::OnCloseDocument();
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibDataToolFileDocument::DoOpenDocument(const ibString& filename)
{
	if (!m_metaData->LoadFromTempFile(filename))
		return false;
	
	//We must delete document after initialization
	return false;
}

bool ibDataToolFileDocument::DoSaveDocument(const ibString& filename)
{
	/*if (!m_metaData->SaveToFile(filename))
		return false;*/

	return true;
}

bool ibDataToolFileDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibDataToolFileDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}