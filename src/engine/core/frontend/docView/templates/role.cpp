#include "role.h"
#include "frontend/mainFrame.h"

// ----------------------------------------------------------------------------
// CRoleEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CRoleEditView, CView);

wxBEGIN_EVENT_TABLE(CRoleEditView, CView)
wxEND_EVENT_TABLE()

#include "frontend/roleEditor/roleEditor.h"

bool CRoleEditView::OnCreate(CDocument* doc, long flags)
{
	m_roleEditor = new CRoleEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_roleEditor->SetReadOnly(flags == wxDOC_READONLY);

	return CView::OnCreate(doc, flags);
}

void CRoleEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_roleEditor != NULL) {
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
		SetFrame(NULL);
	}

	if (CView::OnClose(deleteWindow))
		return m_roleEditor->Destroy();

	return false;
}

// ----------------------------------------------------------------------------
// CGridDocument: wxDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CRoleDocument, CDocument);

bool CRoleDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
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
	return CDocument::IsModified();
}

void CRoleDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// CRoleEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CRoleEditDocument, CDocument);