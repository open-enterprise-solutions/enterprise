#include "visualHost.h"

#include "backend/serialize/dataBuilder.h"
#include "sfrontend/visualView/ctrl/form.h"

void ibVisualHost::CreateVisualHost()
{
	if (ibValueForm* const form = GetValueForm()) {
		SetCaption(form->GetCaption());   // the title bar
		GenerateControl(form);
	}
}

bool ibVisualHost::UpdateVisualHost(ibDataNode& frame)
{
	ibValueForm* const form = GetValueForm();
	if (form == nullptr)
		return false;
	SetCaption(form->GetCaption());   // the title bar
	frame.SetClsid(form->GetClassType());
	frame.SetMetaId(form->EnsureControlID());
	return RefreshControl(form, frame);
}

void ibVisualHost::ClearVisualHost()
{
	if (ibValueForm* const form = GetValueForm())
		ClearControl(form);
}

void ibVisualHost::CreateControl(ibValueFrame* control)
{
	if (control != nullptr)
		GenerateControl(control);
}

void ibVisualHost::RemoveControl(ibValueFrame* control)
{
	if (control != nullptr)
		ClearControl(control);
}

void ibVisualHost::SelectControl(ibValueFrame* control)
{
	if (control != nullptr)
		control->OnSelected(this);
}

void ibVisualHost::GenerateControl(ibValueFrame* control)
{
	control->OnCreate(this);
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		if (ibValueFrame* const child = control->GetChild(idx))
			GenerateControl(child);
	}
	control->OnCreated(this);
}

// ⭐ THE FRAME OF A CONTROL AND ITS SUBTREE — SaveNode's shape (the header, the properties as saved, the children
// as nodes of their class) with each control's State beside its properties (OnUpdate): what a client draws from.
// Not saved anywhere — a reading of the form as it stands.
bool ibVisualHost::RefreshControl(ibValueFrame* control, ibDataNode& node)
{
	// The id is what an event comes back naming, so a control that never had one gets one now.
	node.SetValue(wxT("ControlId"), control->EnsureControlID());
	node.SetValue(wxT("Name"), control->GetControlName());
	if (!control->WriteData(node))
		return false;

	ibDataNode& state = node.Child(wxT("State"));
	control->OnUpdate(state, this);

	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		ibValueFrame* const child = control->GetChild(idx);
		if (child == nullptr)
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->EnsureControlID());
		if (!RefreshControl(child, childNode))
			return false;
	}

	control->OnUpdated(state, this);
	return true;
}

// Children first: a control's cleanup may still reach what it holds.
void ibVisualHost::ClearControl(ibValueFrame* control)
{
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		if (ibValueFrame* const child = control->GetChild(idx))
			ClearControl(child);
	}
	control->OnCleanup(this);
}
