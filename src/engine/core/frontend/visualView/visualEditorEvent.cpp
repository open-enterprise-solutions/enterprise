////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "frontend/docView/docView.h"
#include "frontend/mainFrame.h"
#include "frontend/objinspect/objinspect.h"

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditorCtrl::AddHandler(wxEvtHandler* handler)
{
	m_handlers.push_back(handler);
}

void CVisualEditorNotebook::CVisualEditorCtrl::RemoveHandler(wxEvtHandler* handler)
{
	for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
		if (*it == handler) {
			m_handlers.erase(it);
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyEvent(wxFrameEvent& event, bool forcedelayed)
{
	std::vector< wxEvtHandler* >::iterator handler;
	if (!forcedelayed) {
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++)
			(*handler)->ProcessEvent(event);
		wxView* docView = m_document->GetFirstView();
		if (docView != NULL)
			docView->ProcessEvent(event);
	}
	else {
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++)
			(*handler)->AddPendingEvent(event);
		wxView* docView = m_document->GetFirstView();
		if (docView != NULL)
			docView->AddPendingEvent(event);
	}
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyProjectLoaded()
{
	wxFrameEvent event(wxEVT_PROJECT_LOADED);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyProjectSaved()
{
	wxFrameEvent event(wxEVT_PROJECT_SAVED);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyObjectExpanded(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_EXPANDED, obj);
	NotifyEvent(event);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyObjectSelected(IValueFrame* obj, bool force)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_SELECTED, obj);
	if (force) event.SetString(wxT("force"));

	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyObjectCreated(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_CREATED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyObjectRemoved(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_REMOVED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyPropertyModified(Property* prop)
{
	wxFramePropertyEvent event(wxEVT_PROPERTY_MODIFIED, prop, wxNullVariant);
	NotifyEvent(event, false);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyEventModified(Event* event)
{
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyCodeGeneration(bool panelOnly, bool forcedelayed)
{
	wxFrameEvent event(wxEVT_CODE_GENERATION);
	// Using the previously unused Id field in the event to carry a boolean
	event.SetId((panelOnly ? 1 : 0));
	NotifyEvent(event, forcedelayed);
}

void CVisualEditorNotebook::CVisualEditorCtrl::NotifyProjectRefresh()
{
	wxFrameEvent event(wxEVT_PROJECT_REFRESH);
	NotifyEvent(event, true);
}

#include "core/metadata/metaObjectsDefines.h"

void CVisualEditorNotebook::CVisualEditorCtrl::Execute(CCommand* cmd)
{
	if (m_cmdProc != NULL) {
		m_cmdProc->Execute(cmd);
	}

	IMetaFormObject* formObject = dynamic_cast<IMetaFormObject*>(
		m_document->GetMetaObject()
		);

	wxASSERT(formObject);

	// Create a std::string and copy your document data in to the string    
	if (formObject != NULL) {
		formObject->SaveFormData(m_valueForm);
	}
}

//////////////////////////////////////////////////////////////////////////////////////

wxObject* CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorHost::Create(IValueFrame* control, wxWindow* wxparent)
{
	return control->Create(wxparent, this);
}

void CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorHost::OnCreated(IValueFrame* control, wxObject* obj, wxWindow* wndParent, bool firstÑreated)
{
	control->OnCreated(obj, wndParent, this, firstÑreated);
}

void CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorHost::Update(IValueFrame* control, wxObject* obj)
{
	control->Update(obj, this);
}

void CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorHost::OnUpdated(IValueFrame* control, wxObject* obj, wxWindow* wndParent)
{
	control->OnUpdated(obj, wndParent, this);
}

void CVisualEditorNotebook::CVisualEditorCtrl::CVisualEditorHost::Cleanup(IValueFrame* control, wxObject* obj)
{
	control->Cleanup(obj, this);
}