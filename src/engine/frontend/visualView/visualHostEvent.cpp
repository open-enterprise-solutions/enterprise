////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"
#include "frontend/visualView/ctrl/control.h"

wxObject* ibVisualHost::Create(ibValueFrame *control, wxWindow* wndParent)
{
	return control->Create(wndParent, this);
}

void ibVisualHost::OnCreated(ibValueFrame *control, wxObject* obj, wxWindow* wndParent, bool firstСreated)
{
	control->OnCreated(obj, wndParent, this, firstСreated);
}

void ibVisualHost::OnSelected(ibValueFrame *control, wxObject* obj) 
{
	control->OnSelected(obj); 
}

void ibVisualHost::Update(ibValueFrame *control, wxObject* obj) 
{
	control->Update(obj, this);
}

void ibVisualHost::OnUpdated(ibValueFrame *control, wxObject* obj, wxWindow* wndParent) 
{
	control->OnUpdated(obj, wndParent, this); 
}

void ibVisualHost::Cleanup(ibValueFrame *control, wxObject* obj) 
{
	control->Cleanup(obj, this);
}