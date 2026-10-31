#include "docViewInterface.h"

#include "designer-thin/win/editor/interfaceEditor/interfaceEditor.h"

// ----------------------------------------------------------------------------
// ibInterfaceEditView implementation
// ----------------------------------------------------------------------------

bool ibInterfaceEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_interfaceEditor = new ibInterfaceEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_interfaceEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibInterfaceEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the tree draws itself
}

bool ibInterfaceEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did.
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_interfaceEditor->Freeze();

		m_interfaceEditor->Destroy();
		m_interfaceEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibInterfaceEditDocument implementation
// ----------------------------------------------------------------------------

void ibInterfaceEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocol::ibView::ibCheckTree::Rows) && !patch.Has(ibProtocol::ibView::State))
		return;

	if (ibInterfaceEditor* const interfaceEditor = GetInterfaceEditor())
		interfaceEditor->RefreshInterface(view);
}

ibInterfaceEditor* ibInterfaceEditDocument::GetInterfaceEditor() const
{
	const ibInterfaceEditView* const view = dynamic_cast<const ibInterfaceEditView*>(GetFirstView());
	return view != nullptr ? view->GetInterfaceEditor() : nullptr;
}

ibFrontendView* ibInterfaceEditDocument::DoCreateView()
{
	return new ibInterfaceEditView();
}
