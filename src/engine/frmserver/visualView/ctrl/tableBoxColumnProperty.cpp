#include "tableBox.h"

bool ibValueModelTableBoxColumn::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property == m_propertyView && !IsViewAllowed(static_cast<ibColumnView>(newValue.GetNumber().ToInt())))
		return false;

	return ibValueControl::OnPropertyChanging(property, newValue);
}

// View changed — in the inspector, over MCP or from a script (the live road; the studio's other road reads a column
// anew from its node, ReadData). The child of the new view takes the old one's place, keeping what the two share by name.
void ibValueModelTableBoxColumn::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (property == m_propertyView)
		SetView(GetView());

	// …and a source changed: a view its type does not allow gives way to Input at once, through the door, as the field's
	// does (fieldProperty.cpp) — MCP edits a form nobody draws. Then the parent tells its child, the view that stands now.
	if (property == m_propertySource) {
		if (!IsViewAllowed(GetView()))
			ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(ibColumnView::ibColumnView_Input)));
		m_provider->OnViewChanged();
	}

	ibValueControl::OnPropertyChanged(property, oldValue, newValue);
}

// THE DEFAULT VIEW IS THE ONE THE TYPE GIVES — one Boolean a Flag, any other an Input; changed through the door every
// value goes through (ibPropertyGate), it sets off its own changes.
void ibValueModelTableBoxColumn::SetDefaultView()
{
	const std::vector<ibClassID>& types = GetTypeDesc().GetClsidList();
	const ibColumnView view = types.size() == 1 && types.front() == g_valueBooleanCLSID
		? ibColumnView::ibColumnView_Flag : ibColumnView::ibColumnView_Input;
	ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(view)));
}

// THE VIEWS A COLUMN TAKES (Max, 2026-11-23: "a flag, an input, a label"): Input and Label any type, a Flag one Boolean.
bool ibValueModelTableBoxColumn::IsViewAllowed(ibColumnView view) const
{
	const std::vector<ibClassID>& types = GetTypeDesc().GetClsidList();
	switch (view) {
	case ibColumnView::ibColumnView_Input:
	case ibColumnView::ibColumnView_Label: return true;
	case ibColumnView::ibColumnView_Flag:  return types.size() == 1 && types.front() == g_valueBooleanCLSID;
	default:                               return false;
	}
}
