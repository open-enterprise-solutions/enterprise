#include "docViewDataReportFile.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibReportEditView, ibMetaView);

bool ibReportEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_metaTree = new ibDataReportTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return ibMetaView::OnCreate(doc, flags);
}

void ibReportEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_metaTree->ActivateTree();
}

void ibReportEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxTextCtrl draws itself
}

bool ibReportEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
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

wxIMPLEMENT_DYNAMIC_CLASS(ibReportFileDocument, ibMetaDocument);

bool ibReportFileDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = new ibMetaDataReport();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

#include "frontend/mainFrame/mainFrame.h"

bool ibReportFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}

	objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibReportFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool ibReportFileDocument::DoSaveDocument(const wxString& filename)
{
	if (!GetMetaTree()->Save())
		return false;

	if (!m_metaData->SaveToFile(filename))
		return false;

	return true;
}

bool ibReportFileDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibReportFileDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

ibDataReportTree* ibReportFileDocument::GetMetaTree() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibReportEditView)->GetMetaTree() : nullptr;
}
