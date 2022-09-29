#include "interface.h"
#include "frontend/mainFrame.h"

// ----------------------------------------------------------------------------
// CInterfaceEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CInterfaceEditView, CView);

wxBEGIN_EVENT_TABLE(CInterfaceEditView, CView)
wxEND_EVENT_TABLE()

#define wxID_TEST_INTERFACE 15001

wxMenu* CreateMenuInterface()
{
	// and its menu bar
	wxMenu* menuForm = new wxMenu();
	menuForm->Append(wxID_TEST_INTERFACE, _("Test interface"));
	return menuForm;
}


#include "frontend/interfaceEditor/interfaceEditor.h"

#define formName _("Interfaces")
#define formPos 2

bool CInterfaceEditView::OnCreate(CDocument* doc, long flags)
{
	m_interfaceEditor = new CInterfaceEditor(m_viewFrame, wxID_ANY, doc, doc->GetMetaObject());
	m_interfaceEditor->SetReadOnly(flags == wxDOC_READONLY);

	if (mainFrame->GetMenuBar()->FindMenu(formName) == wxNOT_FOUND) {
		mainFrame->Connect(wxEVT_MENU, wxCommandEventHandler(CInterfaceEditView::OnMenuClick), NULL, this);
		mainFrame->GetMenuBar()->Insert(formPos, CreateMenuInterface(), formName);
	}

	return CView::OnCreate(doc, flags);
}

void CInterfaceEditView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	if (activate && mainFrame->GetMenuBar()->FindMenu(formName) == wxNOT_FOUND) {
		mainFrame->Connect(wxEVT_MENU, wxCommandEventHandler(CInterfaceEditView::OnMenuClick), NULL, this);
		mainFrame->GetMenuBar()->Insert(formPos, CreateMenuInterface(), formName);
	}
	else if (!activate && mainFrame->GetMenuBar()->FindMenu(formName) != wxNOT_FOUND) {
		mainFrame->GetMenuBar()->Remove(formPos);
		mainFrame->Disconnect(wxEVT_MENU, wxCommandEventHandler(CInterfaceEditView::OnMenuClick), NULL, this);
	}

	CView::OnActivateView(activate, activeView, deactiveView);
}

void CInterfaceEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

bool CInterfaceEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

///////////////////////////////////////////////////////////////////////////////

void CInterfaceEditView::OnMenuClick(wxCommandEvent& event)
{
	if (event.GetId() == wxID_TEST_INTERFACE)
		m_interfaceEditor->TestInterface();

	event.Skip();
}

// ----------------------------------------------------------------------------
// CInterfaceDocument: wxDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CInterfaceDocument, CDocument);

bool CInterfaceDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CInterfaceDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool CInterfaceDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool CInterfaceDocument::IsModified() const
{
	return CDocument::IsModified();
}

void CInterfaceDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// CInterfaceEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CInterfaceEditDocument, CDocument);