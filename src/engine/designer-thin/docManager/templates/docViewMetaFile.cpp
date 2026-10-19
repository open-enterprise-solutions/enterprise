#include "docViewMetaFile.h"

#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"
#include "frmclient/docView/tabDocument.h"

bool ibMetadataEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	const ibTabDocument* const tab = dynamic_cast<const ibTabDocument*>(docBase);
	// A configuration browsed in a tab — File → Open (.oap) or Open database configuration: read-only.
	m_metaTree = new ibConfigurationTree(m_viewFrame, tab != nullptr ? tab->GetTabId() : 0, true);
	return ibFrontendView::OnCreate(docBase, flags);
}

void ibMetadataEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate && m_metaTree != nullptr)
		m_metaTree->ActivateTree();
}

void ibMetadataEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, ibConfigurationTree draws itself
}

bool ibMetadataEditView::OnClose(bool deleteWindow)
{
	if (ibFrontendView::OnClose(deleteWindow)) {
		if (m_metaTree != nullptr) {
			m_metaTree->Freeze();
			m_metaTree->Destroy();
			m_metaTree = nullptr;
		}
		return true;
	}
	return false;
}

void ibMetadataBrowserDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	if (ibConfigurationTree* const metaTree = GetMetaTree())
		metaTree->Draw(view.FindChild(ibProtocol::ibView::MetaData));
}

ibConfigurationTree* ibMetadataBrowserDocument::GetMetaTree() const
{
	const ibMetadataEditView* const view = dynamic_cast<const ibMetadataEditView*>(GetFirstView());
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

ibFrontendView* ibMetadataBrowserDocument::DoCreateView()
{
	return new ibMetadataEditView();
}
