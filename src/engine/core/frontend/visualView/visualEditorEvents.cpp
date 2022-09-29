////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "common/docInfo.h"
#include "frontend/mainFrame.h"
#include "frontend/objinspect/objinspect.h"
#include "frontend/objinspect/events.h"

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorContextForm::AddHandler(wxEvtHandler* handler)
{
	m_handlers.push_back(handler);
}

void CVisualEditorContextForm::RemoveHandler(wxEvtHandler* handler)
{
	for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
		if (*it == handler) {
			m_handlers.erase(it);
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

void CVisualEditorContextForm::NotifyEvent(wxFrameEvent& event, bool forcedelayed)
{
	if (!forcedelayed) {
		std::vector< wxEvtHandler* >::iterator handler;
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++) {
			(*handler)->ProcessEvent(event);
		}
		m_view->ProcessEvent(event);
	}
	else {
		std::vector< wxEvtHandler* >::iterator handler;
		for (handler = m_handlers.begin(); handler != m_handlers.end(); handler++) {
			(*handler)->AddPendingEvent(event);
		}
		m_view->AddPendingEvent(event);
	}
}

void CVisualEditorContextForm::NotifyProjectLoaded()
{
	wxFrameEvent event(wxEVT_PROJECT_LOADED);
	NotifyEvent(event);
}

void CVisualEditorContextForm::NotifyProjectSaved()
{
	wxFrameEvent event(wxEVT_PROJECT_SAVED);
	NotifyEvent(event);
}

void CVisualEditorContextForm::NotifyObjectExpanded(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_EXPANDED, obj);
	NotifyEvent(event);
}

void CVisualEditorContextForm::NotifyObjectSelected(IValueFrame* obj, bool force)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_SELECTED, obj);
	if (force) event.SetString(wxT("force"));

	NotifyEvent(event, false);
}

void CVisualEditorContextForm::NotifyObjectCreated(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_CREATED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorContextForm::NotifyObjectRemoved(IValueFrame* obj)
{
	wxFrameObjectEvent event(wxEVT_OBJECT_REMOVED, obj);
	NotifyEvent(event, false);
}

void CVisualEditorContextForm::NotifyPropertyModified(Property* prop)
{
	wxFramePropertyEvent event(wxEVT_PROPERTY_MODIFIED, prop, wxNullVariant);
	NotifyEvent(event, false);
}

void CVisualEditorContextForm::NotifyEventModified(Event* event)
{
}

void CVisualEditorContextForm::NotifyCodeGeneration(bool panelOnly, bool forcedelayed)
{
	wxFrameEvent event(wxEVT_CODE_GENERATION);
	// Using the previously unused Id field in the event to carry a boolean
	event.SetId((panelOnly ? 1 : 0));
	NotifyEvent(event, forcedelayed);
}

void CVisualEditorContextForm::NotifyProjectRefresh()
{
	wxFrameEvent event(wxEVT_PROJECT_REFRESH);
	NotifyEvent(event, true);
}

#include "metadata/metaObjectsDefines.h"

void CVisualEditorContextForm::Execute(CCommand* cmd)
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