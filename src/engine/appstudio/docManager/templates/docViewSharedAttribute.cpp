#include "docViewSharedAttribute.h"

#include "appstudio/win/editor/sharedAttributeEditor/sharedAttributeEditor.h"
#include "protocol/protocolStudio.h"   // ibProtocol::ibView::ibCheckTree

// ----------------------------------------------------------------------------
// ibSharedAttributeEditView implementation
// ----------------------------------------------------------------------------

bool ibSharedAttributeEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_compositionEditor = new ibSharedAttributeCompositionEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_compositionEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibSharedAttributeEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibSharedAttributeEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did.
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_compositionEditor->Freeze();

		m_compositionEditor->Destroy();
		m_compositionEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibSharedAttributeEditDocument implementation
// ----------------------------------------------------------------------------

void ibSharedAttributeEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocol::ibView::ibCheckTree::Rows) && !patch.Has(ibProtocol::ibView::State))
		return;

	if (ibSharedAttributeCompositionEditor* const compositionEditor = GetCompositionEditor())
		compositionEditor->RefreshComposition(view);
}

ibSharedAttributeCompositionEditor* ibSharedAttributeEditDocument::GetCompositionEditor() const
{
	const ibSharedAttributeEditView* const view = dynamic_cast<const ibSharedAttributeEditView*>(GetFirstView());
	return view != nullptr ? view->GetCompositionEditor() : nullptr;
}

ibFrontendView* ibSharedAttributeEditDocument::DoCreateView()
{
	return new ibSharedAttributeEditView();
}

TAB_DOCUMENT_REGISTER(ibSharedAttributeEditDocument, ibProtocol::ibView::ibKind::SharedAttribute)
