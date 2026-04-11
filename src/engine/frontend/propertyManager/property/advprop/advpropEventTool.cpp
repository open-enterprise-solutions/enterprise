#include "advpropEventTool.h"

#include "backend/propertyManager/property/eventAction.h"
#include "backend/propertyManager/property/variant/variantAction.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGEventToolProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGEventToolProperty, wxStringProperty, ComboBoxAndButton);

// register frontend property 
class ibPropertyToolLoader
{
public:
	ibPropertyToolLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGEventToolProperty, ibEventAction::ms_propertyEventAction);
	}
} g_toolLoader;

ibPGEventToolProperty::ibPGEventToolProperty(const wxString& label, const wxString& strName,
	const wxPGChoices& choices,
	const wxVariant& value) : wxPGProperty(label, strName)
{
	ibVariantDataAction* dataAction = property_cast(value, ibVariantDataAction);
	wxASSERT(dataAction);

	m_choices.Assign(choices);
	m_value = wxVariant(0L);
	
	const ibActionDescription& actionDesc = dataAction->GetValueAsActionDesc();
	if (actionDesc.GetSystemAction() != wxNOT_FOUND) {
		for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
			const int val = m_choices.GetValue(i);
			if (val == actionDesc.GetSystemAction()) {
				m_flags &= ~(wxPGPropertyFlags_ActiveButton);
				m_valueBitmapBundle = m_choices.Item(i).GetBitmap();
				m_actionData.SetNumber(val);
				SetValue(value);
				return;
			}
		}
		m_actionData.SetString(value);
		m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.
		SetValue(value);
	}
	else {
		m_actionData.SetString(value);
		m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.
		SetValue(value);
	}
}

ibPGEventToolProperty::~ibPGEventToolProperty()
{
}

wxString ibPGEventToolProperty::ValueToString( wxVariant& value, wxPGPropValFormatFlags flags ) const
{
	ibVariantDataAction* dataAction = property_cast(value, ibVariantDataAction);
	wxASSERT(dataAction);
	const ibActionDescription& actionDesc = dataAction->GetValueAsActionDesc();

	for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
		const int sel_val = m_choices.GetValue(i);
		if (sel_val == actionDesc.GetSystemAction())
			return m_choices.GetLabel(i);
	}

	return actionDesc.GetCustomAction();
}

bool ibPGEventToolProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(wxPGFlags::ComposedValue))
		return wxPGProperty::StringToValue(variant, text, flags);

	if (variant != text) {
		if (text.IsEmpty()) {
			m_actionData.SetString(text);
		}
		variant = new ibVariantDataAction(text);
		return true;
	}

	return false;
}

bool ibPGEventToolProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	value = new ibVariantDataAction(m_choices.GetValue(number));
	
	m_actionData.SetNumber(
		m_choices.GetValue(number)
	);

	return true;
}

void ibPGEventToolProperty::OnSetValue()
{
	if (m_actionData.IsCustomAction()) {
		m_valueBitmapBundle = wxNullBitmap;
		SetFlag(wxPGPropertyFlags_ActiveButton); // Property button always enabled.
	}
	else {
		m_valueBitmapBundle = wxNullBitmap;
		for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
			const int val = m_choices.GetValue(i);
			if (val == m_actionData.GetNumber()) {
				m_valueBitmapBundle = m_choices.Item(i).GetBitmap();
				break;
			}
		}
		ClearFlag(wxPGPropertyFlags_ActiveButton); // Property button always disabled.
	}
}

wxPGEditorDialogAdapter* ibPGEventToolProperty::GetEditorDialog() const
{
	class wxPGEditorEventDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGEventToolProperty* dlgProp = wxDynamicCast(prop, ibPGEventToolProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const wxString& strActionEvent = pg->GetUncommittedPropertyValue();
			if (strActionEvent.IsEmpty()) {
				wxPGProperty* pgProp = pg->GetPropertyByName(wxT("Name"));
				const wxString& strActionEvent =
					(pgProp ? pgProp->GetDisplayedString() : wxString()) + prop->GetName();
				SetValue(new ibVariantDataAction(strActionEvent));
				return true;
			}

			SetValue(new ibVariantDataAction(strActionEvent));
			return true;
		}
	};
	return new wxPGEditorEventDialogAdapter();
}