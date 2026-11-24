#include "field.h"
#include "backend/system/value/valueJob.h"   // g_valueScheduleCLSID — a schedule is shown, not typed

bool ibValueField::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property == m_propertyView && !IsViewAllowed(static_cast<ibFieldView>(newValue.GetNumber().ToInt())))
		return false;

	return ibValueWindow::OnPropertyChanging(property, newValue);
}

// View changed — in the inspector, over MCP or from a script (the live road; the designer's other road reads a field
// anew from its node, ReadData). The child of the new view takes the old one's place, keeping what the two share by
// name; a view's own property changed comes here too, from the child, and the field is drawn anew either way.
void ibValueField::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (property == m_propertyView)
		SetView(GetView());

	// …and a source changed: a view its type does not allow gives way to Input AT ONCE, through the door — not when the
	// field is drawn: MCP edits a form nobody draws, and a field bound back from a Boolean to a String read View '' and
	// refused an Input's TexteditMode (2026-11-24). Then the parent tells its child, the view that stands now.
	if (property == m_propertySource) {
		if (!IsViewAllowed(GetView()))
			ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(ibFieldView::ibFieldView_Input)));
		m_provider->OnViewChanged();
	}

	ibValueWindow::OnPropertyChanged(property, oldValue, newValue);
}

// THE FORM'S BUILDER BINDS A FIELD (formObject.cpp) as a drop binds one (SetBoundSource): the source set and the field
// told of it — its view hears it there (OnViewChanged: an Input's select button is its type's) — then the default view
// taken. Set past the field, a generated field kept a select button on a String (2026-11-24).
void ibValueField::SetSource(const std::vector<ibSourceId>& path)
{
	const ibVariant oldValue = m_propertySource->GetValue();
	m_propertySource->SetValue(ibSourceDescription(path));
	OnPropertyChanged(m_propertySource, oldValue, m_propertySource->GetValue());
	SetDefaultView();
}

// THE DEFAULT VIEW IS THE ONE THE SOURCE'S TYPE GIVES — as the builder once took a control by the type: one Boolean a
// Flag, one schedule a Label (its sentence shown, its editor opened on a click: fourteen fields are not typed), any
// other an Input. Changed through the door every value goes through (ibPropertyGate), it sets off its own changes.
void ibValueField::SetDefaultView()
{
	const std::vector<ibClassID>& types = GetTypeDesc().GetClsidList();
	const ibClassID only = types.size() == 1 ? types.front() : 0;
	ibFieldView view = ibFieldView::ibFieldView_Input;
	if (only == g_valueBooleanCLSID)
		view = ibFieldView::ibFieldView_Flag;
	else if (only == g_valueScheduleCLSID)
		view = ibFieldView::ibFieldView_Label;
	ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(view)));
}

// THE VIEWS BY THE SOURCE'S TYPE (Max, 2026-11-23): Input and Label take any; a Flag a Boolean, a Gauge and a Slider a
// Number — that one type alone; a Switch one type its items can be of (ibIsChoiceType). A composite type is typed into
// or shown, nothing else.
bool ibValueField::IsViewAllowed(ibFieldView view) const
{
	const std::vector<ibClassID>& types = GetTypeDesc().GetClsidList();
	const ibClassID only = types.size() == 1 ? types.front() : 0;
	switch (view) {
	case ibFieldView::ibFieldView_Input:
	case ibFieldView::ibFieldView_Label:  return true;
	case ibFieldView::ibFieldView_Flag:   return only == g_valueBooleanCLSID;
	case ibFieldView::ibFieldView_Gauge:
	case ibFieldView::ibFieldView_Slider: return only == g_valueNumberCLSID;
	case ibFieldView::ibFieldView_Switch: return ibIsChoiceType(only);
	}
	return false;
}
