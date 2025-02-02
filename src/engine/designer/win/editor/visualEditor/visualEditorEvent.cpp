////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "frontend/docView/docView.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::AddHandler(wxEvtHandler* handler)
{
	m_handlers.push_back(handler);
}

void CVisualEditorNotebook::CVisualEditor::RemoveHandler(wxEvtHandler* handler)
{
	for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
		if (*it == handler) {
			m_handlers.erase(it);
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditor::NotifyEvent(wxFrameEvent& event, bool forcedelayed)
{
	std::vector< wxEvtHandler* >::iterator handler;
	if (!forcedelayed) {
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++)
			(*handler)->ProcessEvent(event);
		wxView* docView = m_document->GetFirstView();
		if (docView != nullptr)
			docView->ProcessEvent(event);
	}
	else {
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++)
			(*handler)->AddPendingEvent(event);
		wxView* docView = m_document->GetFirstView();
		if (docView != nullptr)
			docView->AddPendingEvent(event);
	}
}

void CVisualEditorNotebook::CVisualEditor::NotifyProjectLoaded()
{
	wxFrameEvent event(wxEVT_PROJECT_LOADED);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditor::NotifyProjectSaved()
{
	wxFrameEvent event(wxEVT_PROJECT_SAVED);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditor::NotifyObjectExpanded(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_EXPANDED, obj);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditor::NotifyObjectSelected(IValueFrame* obj, bool force)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_SELECTED, obj);
	if (force) event.SetString(wxT("force"));

	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditor::NotifyObjectCreated(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_CREATED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditor::NotifyObjectRemoved(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_REMOVED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditor::NotifyPropertyModified(Property* prop)
{
	wxFramePropertyEvent event(wxEVT_PROPERTY_MODIFIED, prop, wxNullVariant);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditor::NotifyEventModified(Event* event)
{
}

void CVisualEditorNotebook::CVisualEditor::NotifyCodeGeneration(bool panelOnly, bool forcedelayed)
{
	wxFrameEvent event(wxEVT_CODE_GENERATION);
	// Using the previously unused Id field in the event to carry a boolean
	event.SetId((panelOnly ? 1 : 0));
	NotifyEvent(event, forcedelayed);
}

void CVisualEditorNotebook::CVisualEditor::NotifyProjectRefresh()
{
	wxFrameEvent event(wxEVT_PROJECT_REFRESH);
	NotifyEvent(event, true);
}

#include "backend/metaCollection/metaFormObject.h"

void CVisualEditorNotebook::CVisualEditor::Execute(CCommand* cmd)
{
	if (m_cmdProc != nullptr) {
		m_cmdProc->Execute(cmd);
	}

	IMetaObjectForm* formObject = m_document->ConvertMetaObjectToType<IMetaObjectForm>();

	wxASSERT(formObject);

	// Create a std::string and copy your document data in to the string    
	if (formObject != nullptr) {
		formObject->SaveFormData(m_valueForm);
	}
}

//////////////////////////////////////////////////////////////////////////////////////

wxObject* CVisualEditorNotebook::CVisualEditor::CVisualEditorHost::Create(IValueFrame* control, wxWindow* wxparent)
{
	return control->Create(wxparent, this);
}

void CVisualEditorNotebook::CVisualEditor::CVisualEditorHost::OnCreated(IValueFrame* control, wxObject* obj, wxWindow* wndParent, bool firstÑreated)
{
	control->OnCreated(obj, wndParent, this, firstÑreated);
}

void CVisualEditorNotebook::CVisualEditor::CVisualEditorHost::Update(IValueFrame* control, wxObject* obj)
{
	control->Update(obj, this);
}

void CVisualEditorNotebook::CVisualEditor::CVisualEditorHost::OnUpdated(IValueFrame* control, wxObject* obj, wxWindow* wndParent)
{
	control->OnUpdated(obj, wndParent, this);
}

void CVisualEditorNotebook::CVisualEditor::CVisualEditorHost::Cleanup(IValueFrame* control, wxObject* obj)
{
	control->Cleanup(obj, this);
}