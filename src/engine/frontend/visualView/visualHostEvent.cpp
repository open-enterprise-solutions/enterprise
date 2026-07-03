////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"
#include "frontend/visualView/ctrl/control.h"

// Thin forwarders — one body per build, same signature. The per-control
// ibValueFrame::Create / OnCreated / Update / OnUpdated / Cleanup virtuals
// already take ibFrontendWindow*, so the host hooks just relay the call.

// Every lifecycle stage is driven through the control's *WithLayers wrapper. The base
// (ibValueFrame) just forwards to the plain method, so plain controls are unaffected;
// a control with chrome (ibValueControl) builds/updates/tears down its toolbar +
// status bar + search as one grouped unit and routes each stage to its inner window.

wxObject* ibVisualHost::Create(ibValueFrame* control, ibFrontendWindow* wndParent)
{
	return control->CreateWithLayers(wndParent, this);
}

void ibVisualHost::OnCreated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent, bool firstCreated)
{
	control->OnCreatedWithLayers(obj, wndParent, this, firstCreated);
}

void ibVisualHost::OnSelected(ibValueFrame* control, wxObject* obj)
{
	control->OnSelectedWithLayers(obj);
}

void ibVisualHost::Update(ibValueFrame* control, wxObject* obj)
{
	control->UpdateWithLayers(obj, this);
}

void ibVisualHost::OnUpdated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent)
{
	control->OnUpdatedWithLayers(obj, wndParent, this);
}

void ibVisualHost::Cleanup(ibValueFrame* control, wxObject* obj)
{
	control->CleanupWithLayers(obj, this);
}
