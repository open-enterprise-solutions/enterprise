#include "docViewFeatureOption.h"

#include "appstudio/win/editor/featureOptionEditor/featureOptionEditor.h"
#include "protocol/protocolStudio.h"   // ibProtocol::ibView::ibCheckTree

// ----------------------------------------------------------------------------
// ibFeatureOptionEditView implementation
// ----------------------------------------------------------------------------

bool ibFeatureOptionEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	m_membersEditor = new ibFeatureOptionEditor(m_viewFrame, wxID_ANY, dynamic_cast<const ibTabDocument*>(docBase));
	m_membersEditor->SetReadOnly(flags == ibDOC_READONLY);

	return ibFrontendView::OnCreate(docBase, flags);
}

void ibFeatureOptionEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibFeatureOptionEditView::OnClose(bool deleteWindow)
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
// ibFeatureOptionEditDocument implementation
// ----------------------------------------------------------------------------

void ibFeatureOptionEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocol::ibView::ibCheckTree::Rows) && !patch.Has(ibProtocol::ibView::State))
		return;

	if (ibFeatureOptionEditor* const membersEditor = GetMembersEditor())
		membersEditor->RefreshMembers(view);
}

ibFeatureOptionEditor* ibFeatureOptionEditDocument::GetMembersEditor() const
{
	const ibFeatureOptionEditView* const view = dynamic_cast<const ibFeatureOptionEditView*>(GetFirstView());
	return view != nullptr ? view->GetMembersEditor() : nullptr;
}

ibFrontendView* ibFeatureOptionEditDocument::DoCreateView()
{
	return new ibFeatureOptionEditView();
}

TAB_DOCUMENT_REGISTER(ibFeatureOptionEditDocument, ibProtocol::ibView::ibKind::FeatureOption)
