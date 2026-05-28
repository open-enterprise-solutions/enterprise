#include "docViewInterface.h"

// ----------------------------------------------------------------------------
// ibInterfaceEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibInterfaceEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibInterfaceEditView, ibMetaView)
wxEND_EVENT_TABLE()

#include "win/editor/interfaceEditor/interfaceEditor.h"

bool ibInterfaceEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_interfaceEditor = new ibInterfaceEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_interfaceEditor->SetReadOnly(flags == ibDOC_READONLY);

	m_interfaceEditor->RefreshInterface();
	return ibMetaView::OnCreate(doc, flags);
}

void ibInterfaceEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_interfaceEditor != nullptr) 
		m_interfaceEditor->RefreshInterface();	
}

void ibInterfaceEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

bool ibInterfaceEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {
		
		m_interfaceEditor->Freeze();
		
		m_interfaceEditor->Destroy();
		m_interfaceEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibInterfaceDocument: ibDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibInterfaceDocument, ibMetaDocument);

bool ibInterfaceDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibInterfaceDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool ibInterfaceDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool ibInterfaceDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibInterfaceDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// ibInterfaceEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibInterfaceEditDocument, ibMetaDocument);