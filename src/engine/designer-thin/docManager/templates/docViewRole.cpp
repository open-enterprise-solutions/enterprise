#include "docViewRole.h"

#include "designer-thin/win/editor/roleEditor/roleEditor.h"

// ----------------------------------------------------------------------------
// ibRoleEditView implementation
// ----------------------------------------------------------------------------

bool ibRoleEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_roleEditor = new ibRoleEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_roleEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibRoleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the trees draw themselves
}

bool ibRoleEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close), and its window goes with the document when
	// the frame no longer has the tab — not here, as the desktop's did.
	if (ibFrontendView::OnClose(deleteWindow)) {

		m_roleEditor->Freeze();

		m_roleEditor->Destroy();
		m_roleEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibRoleEditDocument implementation
// ----------------------------------------------------------------------------

void ibRoleEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocolName::Rows) && !patch.Has(ibProtocolName::State))
		return;

	if (ibRoleEditor* const roleEditor = GetRoleEditor())
		roleEditor->RefreshRole(view);
}

ibRoleEditor* ibRoleEditDocument::GetRoleEditor() const
{
	const ibRoleEditView* const view = dynamic_cast<const ibRoleEditView*>(GetFirstView());
	return view != nullptr ? view->GetRoleEditor() : nullptr;
}

ibFrontendView* ibRoleEditDocument::DoCreateView()
{
	return new ibRoleEditView();
}
