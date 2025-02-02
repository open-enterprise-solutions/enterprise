#include "role.h"

// ----------------------------------------------------------------------------
// CRoleEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CRoleEditView, CMetaView);

wxBEGIN_EVENT_TABLE(CRoleEditView, CMetaView)
wxEND_EVENT_TABLE()

#include "win/editor/roleEditor/roleEditor.h"

bool CRoleEditView::OnCreate(CMetaDocument* doc, long flags)
{
	m_roleEditor = new CRoleEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_roleEditor->SetReadOnly(flags == wxDOC_READONLY);

	return CMetaView::OnCreate(doc, flags);
}

void CRoleEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_roleEditor != nullptr) {
		m_roleEditor->RefreshRole();
	}
}

void CRoleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

bool CRoleEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (CMetaView::OnClose(deleteWindow)) {
		m_roleEditor->Freeze();
		return m_roleEditor->Destroy();
	}

	return false;
}

// ----------------------------------------------------------------------------
// CGridDocument: wxDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CRoleDocument, CMetaDocument);

bool CRoleDocument::OnCreate(const wxString& path, long flags)
{
	if (!CMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CRoleDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool CRoleDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool CRoleDocument::IsModified() const
{
	return CMetaDocument::IsModified();
}

void CRoleDocument::Modify(bool modified)
{
	CMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// CRoleEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CRoleEditDocument, CMetaDocument);