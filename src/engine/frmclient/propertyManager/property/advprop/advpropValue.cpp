#include "advpropValue.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value, as the server stores it
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties::Id — a choice, named by its id

// -----------------------------------------------------------------------
// ibPGValueProperty
// -----------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibPGValueProperty, wxPGProperty)

namespace {

// The choice a value names by its id — one picked here and not yet the server's; none: wxNOT_FOUND.
long ChosenId(const wxVariant& value)
{
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	if (node.Kind() != ibDataKind::Child)
		return wxNOT_FOUND;
	const std::optional<ibDataNode> chosen = node.AsChild();
	return chosen && chosen->FindField(ibProtocol::ibSchema::ibProperties::Id)
		? static_cast<long>(chosen->GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id)) : wxNOT_FOUND;
}

// The words a value reads as — the server's, or, for a scalar typed here, the text itself.
wxString Presentation(const wxVariant& value)
{
	wxString text;
	if (!value.IsNull())
		value.GetData()->Write(text);
	return text;
}

}

// register frontend property
class ibPropertyValueLoader
{
public:
	ibPropertyValueLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyValue"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGValueProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		});
	}
}g_valueLoader;

ibPGValueProperty::ibPGValueProperty(const wxString& label, const wxString& name,
	const ibPropertyChoiceList& choices, const wxVariant& value)
	: wxPGProperty(label, name)
{
	wxPGChoices ch;
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
		ch.Add(choices.GetLabel(idx), choices.GetId(idx));
	m_choices.Assign(ch);
	SetValue(value);
}

const wxPGEditor* ibPGValueProperty::DoGetEditorClass() const
{
	return m_choices.GetCount() > 0 ? wxPGEditor_Choice : wxPGEditor_TextCtrl;
}

wxString ibPGValueProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	const long id = ChosenId(value);
	if (id != wxNOT_FOUND) {
		const int index = m_choices.Index(id);
		return index != wxNOT_FOUND ? m_choices.GetLabel(index) : wxString();
	}
	return Presentation(value);
}

bool ibPGValueProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	// Typed in — the server reads it as a value of the binding's type; empty: no value.
	variant = wxVariant(new ibVariantDataSerializable(variant.GetType(), ibDataValue::String(text), text));
	return true;
}

bool ibPGValueProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	if (number < 0 || static_cast<unsigned int>(number) >= m_choices.GetCount())
		return false;
	auto chosen = std::make_shared<ibDataNode>();
	chosen->SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(m_choices.GetValue(number)));
	value = wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Child(chosen), m_choices.GetLabel(number)));
	return true;
}

int ibPGValueProperty::GetChoiceSelection() const
{
	const long id = ChosenId(m_value);
	if (id != wxNOT_FOUND)
		return m_choices.Index(id);
	// The server's value, by the words it reads as.
	const wxString text = Presentation(m_value);
	for (unsigned int idx = 0; idx < m_choices.GetCount(); idx++) {
		if (m_choices.GetLabel(idx).IsSameAs(text, false))
			return static_cast<int>(idx);
	}
	return wxNOT_FOUND;
}
