#include "docViewDataToolFile.h"

#include "appstudio/mainFrame/metaTree/treeDataTool.h"

bool ibDataToolEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	const ibTabDocument* const tab = dynamic_cast<const ibTabDocument*>(doc);
	m_metaTree = new ibDataToolTree(m_viewFrame, tab != nullptr ? tab->GetTabId() : 0);

	return ibFrontendView::OnCreate(doc, flags);
}

void ibDataToolEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate) m_metaTree->ActivateTree();
}

void ibDataToolEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the tree draws itself
}

void ibDataToolFileDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	if (ibDataToolTree* const metaTree = GetMetaTree())
		metaTree->Draw(view.FindChild(ibProtocol::ibView::MetaData));
}

ibDataToolTree* ibDataToolFileDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it: a first view of another kind makes the cast yield null.
	const ibDataToolEditView* const view = dynamic_cast<const ibDataToolEditView*>(GetFirstView());
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

ibFrontendView* ibDataToolFileDocument::DoCreateView()
{
	return new ibDataToolEditView();
}

TAB_DOCUMENT_REGISTER(ibDataToolFileDocument, ibProtocol::ibView::ibKind::DataTool)
