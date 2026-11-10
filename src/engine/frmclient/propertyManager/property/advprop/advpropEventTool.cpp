#include "advpropEventTool.h"

#include "core/stringUtils.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the action's node
#include "core/description/standardCommandDescription.h"                              // ibActionDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGEventToolProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGEventToolProperty, ibStringProperty, ComboBoxAndButton);

namespace {

// The action a value holds, as the server stores it…
ibStandardCommandDescription ReadAction(const ibVariant& value)
{
	ibStandardCommandDescription actionDesc(wxNOT_FOUND);
	ibActionDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), actionDesc);
	return actionDesc;
}

// …and the value of another one: the same property's, read as its handler's name (a standard command's is the
// choice's — ValueToString).
ibVariant WriteAction(const ibStandardCommandDescription& actionDesc)
{
	ibDataValue node;
	ibActionDescriptionMemory::WriteNode(node, actionDesc);
	return ibVariant(new ibVariantSerializable(node, actionDesc.GetCustomAction()));
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
		ibPropertyRegistry::Register(wxT("ibEventAction"), [](ibBackendProperty* property) -> ibPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGEventToolProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		});
	}
} g_toolLoader;

ibPGEventToolProperty::ibPGEventToolProperty(const wxString& label, const wxString& strName,
	const ibPropertyChoiceList& choices,
	const ibVariant& value) : ibPGProperty(label, strName)
{
	ibPGChoices ch;
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		ibPGChoiceEntry item(choices.GetLabel(idx), choices.GetId(idx));
		item.SetBitmap(choices.GetBitmap(idx));
		ch.Add(item);
	}
	m_choices.Assign(ch);
	m_value = ibVariant(0L);

	const ibStandardCommandDescription actionDesc = ReadAction(value);
	if (actionDesc.GetSystemAction() != wxNOT_FOUND) {
		for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
			const int val = m_choices.GetValue(i);
			if (val == actionDesc.GetSystemAction()) {
				m_flags &= ~(ibPGPropertyFlags_ActiveButton);
				m_valueBitmapBundle = m_choices.Item(i).GetBitmap();
				m_actionData.SetNumber(val);
				SetValue(value);
				return;
			}
		}
		m_actionData.SetString(value.GetString());
		m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.
		SetValue(value);
	}
	else {
		m_actionData.SetString(value.GetString());
		m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.
		SetValue(value);
	}
}

ibPGEventToolProperty::~ibPGEventToolProperty()
{
}

wxString ibPGEventToolProperty::ValueToString( ibVariant& value, ibPGPropValFormatFlags flags ) const
{
	const ibStandardCommandDescription actionDesc = ReadAction(value);

	for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
		const int sel_val = m_choices.GetValue(i);
		if (sel_val == actionDesc.GetSystemAction())
			return m_choices.GetLabel(i);
	}

	return actionDesc.GetCustomAction();
}

bool ibPGEventToolProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(ibPGFlags::ComposedValue))
		return ibPGProperty::StringToValue(variant, text, flags);

	if (variant != text) {
		if (text.IsEmpty()) {
			m_actionData.SetString(text);
		}
		variant = WriteAction(ibStandardCommandDescription(text));
		return true;
	}

	return false;
}

bool ibPGEventToolProperty::IntToValue(ibVariant& value, int number, ibPGPropValFormatFlags flags) const
{
	value = WriteAction(ibStandardCommandDescription(static_cast<ibActionID>(m_choices.GetValue(number))));

	m_actionData.SetNumber(
		m_choices.GetValue(number)
	);

	return true;
}

void ibPGEventToolProperty::OnSetValue()
{
	if (m_actionData.IsCustomAction()) {
		m_valueBitmapBundle = wxNullBitmap;
		SetFlag(ibPGPropertyFlags_ActiveButton); // Property button always enabled.
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
		ClearFlag(ibPGPropertyFlags_ActiveButton); // Property button always disabled.
	}
}

ibPGEditorDialogAdapter* ibPGEventToolProperty::GetEditorDialog() const
{
	class ibPGEditorEventDialogAdapter : public ibPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) wxOVERRIDE
		{
			ibPGEventToolProperty* dlgProp = wxDynamicCast(prop, ibPGEventToolProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const wxString strActionEvent = pg->GetUncommittedPropertyValue().GetString();
			if (strActionEvent.IsEmpty()) {
				ibPGProperty* pgProp = pg->GetPropertyByName(wxT("Name"));
				const wxString& strActionEvent =
					(pgProp ? pgProp->GetDisplayedString() : wxString()) + prop->GetName();
				SetValue(WriteAction(ibStandardCommandDescription(strActionEvent)));
				return true;
			}

			SetValue(WriteAction(ibStandardCommandDescription(strActionEvent)));
			return true;
		}
	};
	return new ibPGEditorEventDialogAdapter();
}
