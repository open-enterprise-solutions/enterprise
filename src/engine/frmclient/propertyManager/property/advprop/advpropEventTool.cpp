#include "advpropEventTool.h"

#include "core/stringUtils.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the action's node
#include "core/description/standardCommandDescription.h"                              // ibActionDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGEventToolProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGEventToolProperty, wxStringProperty, ComboBoxAndButton);

namespace {

// The action a value holds, as the server stores it…
ibStandardCommandDescription ReadAction(const wxVariant& value)
{
	ibStandardCommandDescription actionDesc(wxNOT_FOUND);
	ibActionDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), actionDesc);
	return actionDesc;
}

// …and the value of another one: the same property's, read as its handler's name (a standard command's is the
// choice's — ValueToString).
wxVariant WriteAction(const wxVariant& value, const ibStandardCommandDescription& actionDesc)
{
	ibDataValue node;
	ibActionDescriptionMemory::WriteNode(node, actionDesc);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, actionDesc.GetCustomAction()));
}

} // namespace

// register frontend property
class ibPropertyToolLoader
{
public:
	ibPropertyToolLoader()
	{
		// The server's ibEventAction — its node the action as the core writes it (ibActionDescriptionMemory), its
		// choices the standard commands it may run.
		ibPropertyRegistry::Register(wxT("ibEventAction"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGEventToolProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		});
	}
} g_toolLoader;

ibPGEventToolProperty::ibPGEventToolProperty(const wxString& label, const wxString& strName,
	const ibPropertyChoiceList& choices,
	const wxVariant& value) : wxPGProperty(label, strName)
{
	wxPGChoices ch;
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		wxPGChoiceEntry item(choices.GetLabel(idx), choices.GetId(idx));
		item.SetBitmap(choices.GetBitmap(idx));
		ch.Add(item);
	}
	m_choices.Assign(ch);
	m_value = wxVariant(0L);

	const ibStandardCommandDescription actionDesc = ReadAction(value);
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
	const ibStandardCommandDescription actionDesc = ReadAction(value);

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
		variant = WriteAction(variant, ibStandardCommandDescription(text));
		return true;
	}

	return false;
}

bool ibPGEventToolProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	value = WriteAction(value, ibStandardCommandDescription(static_cast<ibActionID>(m_choices.GetValue(number))));

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
				SetValue(WriteAction(dlgProp->GetValue(), ibStandardCommandDescription(strActionEvent)));
				return true;
			}

			SetValue(WriteAction(dlgProp->GetValue(), ibStandardCommandDescription(strActionEvent)));
			return true;
		}
	};
	return new wxPGEditorEventDialogAdapter();
}
