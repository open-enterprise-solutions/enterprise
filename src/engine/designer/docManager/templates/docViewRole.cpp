#include "docViewRole.h"

// ----------------------------------------------------------------------------
// ibRoleEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibRoleEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibRoleEditView, ibMetaView)
wxEND_EVENT_TABLE()

#include "win/editor/roleEditor/roleEditor.h"

bool ibRoleEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_roleEditor = new ibRoleEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_roleEditor->SetReadOnly(flags == ibDOC_READONLY);
	
	m_roleEditor->RefreshRole();	
	return ibMetaView::OnCreate(doc, flags);
}

void ibRoleEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_roleEditor != nullptr) 
		m_roleEditor->RefreshRole();	
}

void ibRoleEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

bool ibRoleEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {
	
		m_roleEditor->Freeze();
	
		m_roleEditor->Destroy();
		m_roleEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// CGridDocument: ibDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibRoleDocument, ibMetaDocument);

bool ibRoleDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibRoleDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool ibRoleDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool ibRoleDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibRoleDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// ibRoleEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibRoleEditDocument, ibMetaDocument);