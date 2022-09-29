////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditorBase.h"
#include "frontend/visualView/controls/baseControl.h"

wxObject* IVisualHost::Create(IValueFrame *control, wxObject* parent) 
{
	return control->Create(parent, this);
}

void IVisualHost::OnCreated(IValueFrame *control, wxObject* obj, wxWindow* wndParent, bool firstÑreated)
{
	control->OnCreated(obj, wndParent, this, firstÑreated);
}

void IVisualHost::OnSelected(IValueFrame *control, wxObject* obj) 
{
	control->OnSelected(obj); 
}

void IVisualHost::Update(IValueFrame *control, wxObject* obj) 
{
	control->Update(obj, this);
}

void IVisualHost::OnUpdated(IValueFrame *control, wxObject* obj, wxWindow* wndParent) 
{
	control->OnUpdated(obj, wndParent, this); 
}

void IVisualHost::Cleanup(IValueFrame *control, wxObject* obj) 
{
	control->Cleanup(obj, this);
}