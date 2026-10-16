#include "docViewCommonAttribute.h"

#include "designer-thin/win/editor/commonAttributeEditor/commonAttributeEditor.h"

// ----------------------------------------------------------------------------
// ibCommonAttributeEditView implementation
// ----------------------------------------------------------------------------

bool ibCommonAttributeEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_compositionEditor = new ibCommonAttributeCompositionEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_compositionEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibCommonAttributeEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibCommonAttributeEditView::OnClose(bool deleteWindow)
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
// ibCommonAttributeEditDocument implementation
// ----------------------------------------------------------------------------

void ibCommonAttributeEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocolName::Rows) && !patch.Has(ibProtocolName::State))
		return;

	if (ibCommonAttributeCompositionEditor* const compositionEditor = GetCompositionEditor())
		compositionEditor->RefreshComposition(view);
}

ibCommonAttributeCompositionEditor* ibCommonAttributeEditDocument::GetCompositionEditor() const
{
	const ibCommonAttributeEditView* const view = dynamic_cast<const ibCommonAttributeEditView*>(GetFirstView());
	return view != nullptr ? view->GetCompositionEditor() : nullptr;
}

ibFrontendView* ibCommonAttributeEditDocument::DoCreateView()
{
	return new ibCommonAttributeEditView();
}
