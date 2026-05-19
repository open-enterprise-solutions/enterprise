////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"
#include "frontend/visualView/ctrl/control.h"

// Thin forwarders — one body per build, same signature. The per-control
// ibValueFrame::Create / OnCreated / Update / OnUpdated / Cleanup virtuals
// already take ibFrontendWindow*, so the host hooks just relay the call.

wxObject* ibVisualHost::Create(ibValueFrame* control, ibFrontendWindow* wndParent)
{
	return control->Create(wndParent, this);
}

void ibVisualHost::OnCreated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent, bool firstCreated)
{
	control->OnCreated(obj, wndParent, this, firstCreated);
}

void ibVisualHost::OnSelected(ibValueFrame* control, wxObject* obj)
{
	control->OnSelected(obj);
}

void ibVisualHost::Update(ibValueFrame* control, wxObject* obj)
{
	control->Update(obj, this);
}

void ibVisualHost::OnUpdated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent)
{
	control->OnUpdated(obj, wndParent, this);
}

void ibVisualHost::Cleanup(ibValueFrame* control, wxObject* obj)
{
	control->Cleanup(obj, this);
}
