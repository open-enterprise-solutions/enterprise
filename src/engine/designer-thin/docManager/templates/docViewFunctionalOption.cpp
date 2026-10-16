#include "docViewFunctionalOption.h"

#include "designer-thin/win/editor/functionalOptionEditor/functionalOptionEditor.h"

// ----------------------------------------------------------------------------
// ibFunctionalOptionEditView implementation
// ----------------------------------------------------------------------------

bool ibFunctionalOptionEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_membersEditor = new ibFunctionalOptionEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_membersEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibFunctionalOptionEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibFunctionalOptionEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did.
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_membersEditor->Freeze();

		m_membersEditor->Destroy();
		m_membersEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibFunctionalOptionEditDocument implementation
// ----------------------------------------------------------------------------

void ibFunctionalOptionEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocolName::Rows) && !patch.Has(ibProtocolName::State))
		return;

	if (ibFunctionalOptionEditor* const membersEditor = GetMembersEditor())
		membersEditor->RefreshMembers(view);
}

ibFunctionalOptionEditor* ibFunctionalOptionEditDocument::GetMembersEditor() const
{
	const ibFunctionalOptionEditView* const view = dynamic_cast<const ibFunctionalOptionEditView*>(GetFirstView());
	return view != nullptr ? view->GetMembersEditor() : nullptr;
}

ibFrontendView* ibFunctionalOptionEditDocument::DoCreateView()
{
	return new ibFunctionalOptionEditView();
}
