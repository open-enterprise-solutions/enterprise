#include "docViewMetaFile.h"

#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

bool ibMetadataEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	// The desktop's `new ibConfigurationTree(doc, m_viewFrame)` and SetReadOnly(true) — a tree over the configuration
	// the tab holds (File → Open, Open database configuration), its verbs shut; drawn of the tab's rows, as an
	// external file's tree is.
	const ibTabDocument* const tab = dynamic_cast<const ibTabDocument*>(docBase);
	m_metaTree = new ibConfigurationTree(m_viewFrame, tab != nullptr ? tab->GetTabId() : 0);
	m_metaTree->SetReadOnly(true);
	return ibFrontendView::OnCreate(docBase, flags);
}

void ibMetadataEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate && m_metaTree != nullptr) m_metaTree->ActivateTree();
}

void ibMetadataEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, ibConfigurationTree draws itself
}

bool ibMetadataEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did.
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

// ----------------------------------------------------------------------------
// ibMetadataBrowserDocument implementation
// ----------------------------------------------------------------------------

void ibMetadataBrowserDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	if (ibConfigurationTree* const metaTree = GetMetaTree())
		metaTree->Draw(view.FindChild(ibProtocol::ibView::MetaData));
}

ibConfigurationTree* ibMetadataBrowserDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it: a first view of another kind makes the cast yield null.
	const ibMetadataEditView* const view = dynamic_cast<const ibMetadataEditView*>(GetFirstView());
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

ibFrontendView* ibMetadataBrowserDocument::DoCreateView()
{
	return new ibMetadataEditView();
}
