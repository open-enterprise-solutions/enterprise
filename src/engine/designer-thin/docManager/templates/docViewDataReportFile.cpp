#include "docViewDataReportFile.h"

#include "designer-thin/mainFrame/metaTree/treeDataReport.h"

bool ibReportEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	const ibTabDocument* const tab = dynamic_cast<const ibTabDocument*>(doc);
	m_metaTree = new ibDataReportTree(m_viewFrame, tab != nullptr ? tab->GetTabId() : 0);

	return ibFrontendView::OnCreate(doc, flags);
}

void ibReportEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate) m_metaTree->ActivateTree();
}

void ibReportEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the tree draws itself
}

void ibReportFileDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	if (ibDataReportTree* const metaTree = GetMetaTree())
		metaTree->Draw(view.FindChild(ibProtocol::ibView::MetaData));
}

ibDataReportTree* ibReportFileDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it: a first view of another kind makes the cast yield null.
	const ibReportEditView* const view = dynamic_cast<const ibReportEditView*>(GetFirstView());
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

ibFrontendView* ibReportFileDocument::DoCreateView()
{
	return new ibReportEditView();
}
