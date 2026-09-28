#include "docViewFunctionalOption.h"

// ----------------------------------------------------------------------------
// ibFunctionalOptionEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFunctionalOptionEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibFunctionalOptionEditView, ibMetaView)
wxEND_EVENT_TABLE()

#include "win/editor/functionalOptionEditor/functionalOptionEditor.h"

bool ibFunctionalOptionEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_membersEditor = new ibFunctionalOptionEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_membersEditor->SetReadOnly(flags == ibDOC_READONLY);

	m_membersEditor->RefreshMembers();
	return ibView::OnCreate(docBase, flags);
}

void ibFunctionalOptionEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_membersEditor != nullptr)
		m_membersEditor->RefreshMembers();
}

void ibFunctionalOptionEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibFunctionalOptionEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		m_membersEditor->Freeze();

		m_membersEditor->Destroy();
		m_membersEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibFunctionalOptionDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibFunctionalOptionDocument, ibMetaDocument);

bool ibFunctionalOptionDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

// The members keep the membership, and they are saved with the configuration — nothing to write here.
bool ibFunctionalOptionDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool ibFunctionalOptionDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool ibFunctionalOptionDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibFunctionalOptionDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// ibFunctionalOptionEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFunctionalOptionEditDocument, ibFunctionalOptionDocument);
