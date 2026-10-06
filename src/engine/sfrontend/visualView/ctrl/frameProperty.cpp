#include "frame.h"

bool ibValueFrame::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
}

bool ibValueFrame::OnEventChanging(ibEvent* event, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue)
{
}
