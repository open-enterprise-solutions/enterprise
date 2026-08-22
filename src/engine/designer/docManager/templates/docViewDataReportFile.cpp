#include "docViewDataReportFile.h"

#include "docViewComposer.h"   // ibCommitOpenComposers — a composer tab lands before the tree is written

wxIMPLEMENT_DYNAMIC_CLASS(ibReportEditView, ibMetaView);

bool ibReportEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_metaTree = new ibDataReportTree(doc, m_viewFrame);
	m_metaTree->SetReadOnly(false);

	return ibView::OnCreate(docBase, flags);
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

wxIMPLEMENT_DYNAMIC_CLASS(ibReportFileDocument, ibMetaDataDocument);

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
	// ⭐ THE OPEN EDITORS LAND BEFORE THE TREE IS WRITTEN. A composer tab edits into a transactional
	// buffer and used to put it onto the metaobject only when it CLOSED — so saving an external
	// report from its tree wrote the composition as it was before the grouping was added. A panel
	// that objects stops the save the same way it stops a close.
	if (!ibCommitOpenComposers())
		return false;

	// The tree writes the report's own name / synonym / comment onto the metaobject, and saves the
	// image when something changed. It answers FALSE for "there was nothing to save" — which is not
	// a failure: reading it as one meant an unchanged report could not be written to a file at all.
	if (ibDataReportTree* metaTree = GetMetaTree())
		metaTree->Save();

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
	// GUARD THE CAST, not the pointer that went into it — see the twin in docViewDataProcessorFile.cpp.
	ibReportEditView* view = wxDynamicCast(GetFirstView(), ibReportEditView);
	return view != nullptr ? view->GetMetaTree() : nullptr;
}
