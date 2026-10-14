#include "docViewDataProcessorFile.h"

#include "designer-thin/mainFrame/metaTree/treeDataProcessor.h"

bool ibDataProcessorEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	const ibTabDocument* const tab = dynamic_cast<const ibTabDocument*>(doc);
	m_metaTree = new ibDataProcessorTree(m_viewFrame, tab != nullptr ? tab->GetTabId() : 0);

	return ibFrontendView::OnCreate(doc, flags);
}

void ibDataProcessorEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate) m_metaTree->ActivateTree();
}

void ibDataProcessorEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the tree draws itself
}

void ibDataProcessorFileDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	if (ibDataProcessorTree* const metaTree = GetMetaTree())
		metaTree->Draw(view.FindChild(ibProtocolName::MetaData));
}

ibDataProcessorTree* ibDataProcessorFileDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it: a first view of another kind makes the cast yield null.
	const ibDataProcessorEditView* const view = dynamic_cast<const ibDataProcessorEditView*>(GetFirstView());
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

ibFrontendView* ibDataProcessorFileDocument::DoCreateView()
{
	return new ibDataProcessorEditView();
}
