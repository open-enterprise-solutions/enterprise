#include "frame.h"
#include "frmserver/visualView/visualHost.h"   // ibFormVisualEditView — the host the control's form is held by

bool ibValueFrame::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// The control shows otherwise now: the next frame draws it anew.
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);
}

bool ibValueFrame::OnEventChanging(ibEvent* event, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue)
{
}
