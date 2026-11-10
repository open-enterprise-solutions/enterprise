#include "advpropType.h"
#include "advpropValuePicture.h"                                         // the value's picture, read when drawn

#include "frmclient/backend/backend_core.h"                              // MAX_PRECISION_NUMBER, MAX_LENGTH_STRING
#include "frmclient/backend/compiler/value.h"                            // a type's name and picture, as the server named it
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the type's node
#include "core/description/typeDescription.h"                                         // ibTypeDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_* — the grid flags
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGTypeProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGTypeProperty, ibStringProperty, ComboBoxAndButton)

namespace {

// The type a value holds, as the server stores it.
ibTypeDescription ReadType(const ibVariant& value)
{
	ibTypeDescription typeDesc;
	ibTypeDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), typeDesc);
	return typeDesc;
}

// …and the value of another one: the same property's, read as the names of its types. Written OVER the node it came
// in, not into a new one: the holder of an attribute (ibPropertyPredefinedType) hands over the attribute's whole node with
// the type in it, and takes the same shape back; a type property's node is the type alone.
ibVariant WriteType(const ibVariant& value, const ibTypeDescription& typeDesc)
{
	ibDataValue type;
	ibTypeDescriptionMemory::WriteNode(type, typeDesc);
	const std::optional<ibDataNode> held = ibVariantSerializable::GetNodeValue(value).AsChild();
	auto written = std::make_shared<ibDataNode>(held ? *held : ibDataNode());
	if (const std::optional<ibDataNode> fields = type.AsChild())
		for (const auto& field : fields->Fields())
			written->SetField(field.first, field.second);
	const ibDataValue node = ibDataValue::Child(written);
	wxString names;
	for (const ibClassID clsid : typeDesc.GetClsidList()) {
		const ibCtorAbstractType* const typeCtor = ibValue::GetAvailableCtor(clsid);
		if (typeCtor != nullptr)
			names = names.IsEmpty() ? typeCtor->GetClassName() : names + wxT(", ") + typeCtor->GetClassName();
	}
	return ibVariant(new ibVariantSerializable(node, names));
}

}

// register frontend property
class ibPropertyTypeLoader
{
public:
	ibPropertyTypeLoader()
	{
		// ibPropertyType — the type description as the server stores it (ibTypeDescriptionMemory); what it may hold is the
		// row's offer (GetValueList: each type's class id, in the server's order; several at once unless a table's shape).
		const auto editor = [](ibBackendProperty* property) -> ibPGProperty* {
			ibPropertyChoiceList offered;
			const bool multiple = property->GetValueList(offered) == ibPropertyChoiceMode::Mult;
			std::vector<ibClassID> listType;
			for (unsigned int idx = 0; idx < offered.GetCount(); idx++) {
				ibClassID clsid = 0;
				offered.GetValue(idx).GetNumber().ToInt(clsid);
				listType.push_back(clsid);
			}
			return new ibPGTypeProperty(listType, multiple, property->GetLabel(), property->GetName(), property->GetValue());
		};
		ibPropertyRegistry::Register(wxT("ibPropertyType"), editor);
		// …and the holder of an attribute whose type is chosen - a code, a description, a number: the same editor, over
		// the attribute's node (WriteType).
		ibPropertyRegistry::Register(wxT("ibPropertyPredefinedType"), editor);
	}
}g_typeLoader;

ibPGChoices ibPGTypeProperty::GetDateTime()
{
	ibPGChoices choices;
	choices.Add(_("Date"), ibDateFractions::ibDateFractions_Date);
	choices.Add(_("Date and time"), ibDateFractions::ibDateFractions_DateTime);
	choices.Add(_("Time"), ibDateFractions::ibDateFractions_Time);
	return choices;
}

ibPGTypeProperty::ibPGTypeProperty(const std::vector<ibClassID>& listType, bool multiple, const wxString& label,
	const wxString& strName, const ibVariant& value) :
	ibPGProperty(label, strName), m_listType(listType), m_multiple(multiple)
{
	m_precision = new ibUIntProperty(_("Precision"), wxT("precision"), 0);
	AddPrivateChild(m_precision);
	m_scale = new ibUIntProperty(_("Scale"), wxT("scale"), 0);
	AddPrivateChild(m_scale);
	m_nonNegative = new ibBoolProperty(_("Non-negative"), wxT("non_negative"), false);
	m_nonNegative->SetAttribute(ibPG_BOOL_USE_CHECKBOX, true);
	AddPrivateChild(m_nonNegative);
	{ ibPGChoices dtChoices = GetDateTime();
	m_date_time = new ibEnumProperty(_("Date time"), wxT("date_time"), dtChoices, ibDateFractions::ibDateFractions_Date); }
	AddPrivateChild(m_date_time);
	m_length = new ibUIntProperty(_("Length"), wxT("length"), 0);
	AddPrivateChild(m_length);
	{ ibPGChoices allowedChoices;
	allowedChoices.Add(_("Variable"), ibAllowedLength::ibAllowedLength_Variable);
	allowedChoices.Add(_("Fixed"), ibAllowedLength::ibAllowedLength_Fixed);
	m_allowedLength = new ibEnumProperty(_("Allowed length"), wxT("allowed_length"), allowedChoices,
		ibAllowedLength::ibAllowedLength_Variable); }
	AddPrivateChild(m_allowedLength);
	m_unlimited = new ibBoolProperty(_("Unlimited length"), wxT("unlimited"), false);
	m_unlimited->SetAttribute(ibPG_BOOL_USE_CHECKBOX, true);
	AddPrivateChild(m_unlimited);

	// Each type it may hold, named and drawn as the server offered it — in the server's order (GetTypesByFilter there:
	// the primitives, the containers, each family before its members, AnyRef last).
	for (const ibClassID& clsid : m_listType) {
		const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;
		auto choice = m_choices.Add(so->GetClassName(), so->GetClassIcon());
		m_valChoices.insert_or_assign(choice.GetValue(), clsid);
	}

	SetValue(value);

	//m_flags |= ibPGFlags::ReadOnly;
	m_flags |= ibPGPropertyFlags_ActiveButton;
}

bool ibPGTypeProperty::IntToValue(ibVariant& value, int number, ibPGPropValFormatFlags flags) const
{
	ibTypeDescription td = ReadType(value);
	td.SetDefaultMetaType(m_valChoices.at(number));
	value = WriteType(value, td);
	return true;
}

ibVariant ibPGTypeProperty::ChildChanged(ibVariant& thisValue, int childIndex, ibVariant& childValue) const
{
	// The child by itself, not by its place among the children: each setter is given the qualifiers it does not change
	// as they are - SetNumber and SetString take the rest at their defaults, and a length changed lost the allowed one
	ibTypeDescription td = ReadType(thisValue);
	const ibPGProperty* const child = Item(static_cast<unsigned int>(childIndex));
	if (child == m_precision || child == m_scale) {
		// a length the column cannot take, or none, is refused; a scale past the length stops at it - the type
		// description's own rule (ibQualifierNumber::ScaleWithin)
		long precision = child == m_precision ? childValue.GetInteger() : td.GetPrecision();
		const long scale = child == m_scale ? childValue.GetInteger() : td.GetScale();
		if (precision > MAX_PRECISION_NUMBER || precision == 0)
			precision = td.GetPrecision();
		td.SetNumber(precision, scale, td.IsNonNegative());
	}
	else if (child == m_nonNegative) {
		td.SetNumber(td.GetPrecision(), td.GetScale(), childValue.GetBoolean());
	}
	else if (child == m_date_time) {
		td.SetDate((ibDateFractions)childValue.GetInteger());
	}
	else if (child == m_length) {
		// 0 is no limit - the check comes on with it (RefreshChildren)
		long length = childValue.GetInteger();
		if (length > MAX_LENGTH_STRING)
			length = td.GetLength();
		td.SetString(length, length > 0 ? td.GetAllowedLength() : ibAllowedLength::ibAllowedLength_Variable);
	}
	else if (child == m_allowedLength) {
		td.SetString(td.GetLength(), (ibAllowedLength)childValue.GetInteger());
	}
	else if (child == m_unlimited) {
		// on: the length 0, held while it stays on (Max, 2026-11-10); off: a new attribute's length
		td.SetString(childValue.GetBoolean() ? 0 : ibQualifierString().m_length, ibAllowedLength::ibAllowedLength_Variable);
	}
	return WriteType(thisValue, td);
}

void ibPGTypeProperty::RefreshChildren()
{
	const ibTypeDescription td = ReadType(m_value);

	// THE QUALIFIERS OF THE ONE TYPE IT HOLDS — none for several types, or for none; a string's length and allowed
	// length held while it has no limit (the length 0)
	const ibValueTypes held = td.GetClsidCount() == 1 ? ibValue::GetVTByID(td.GetFirstClsid()) : ibValueTypes::TYPE_EMPTY;
	const auto show = [](ibPGProperty* child, bool shown) {
		child->Hide(!shown);
		child->SetExpanded(shown);
	};
	show(m_precision, held == ibValueTypes::TYPE_NUMBER);
	show(m_scale, held == ibValueTypes::TYPE_NUMBER);
	show(m_nonNegative, held == ibValueTypes::TYPE_NUMBER);
	show(m_date_time, held == ibValueTypes::TYPE_DATE);
	show(m_length, held == ibValueTypes::TYPE_STRING);
	show(m_allowedLength, held == ibValueTypes::TYPE_STRING);
	show(m_unlimited, held == ibValueTypes::TYPE_STRING);

	m_precision->SetValue(td.GetPrecision());
	m_scale->SetValue(td.GetScale());
	m_nonNegative->SetValue(td.IsNonNegative());
	m_date_time->SetValue(td.GetDateFraction());
	m_length->SetValue(td.GetLength());
	m_allowedLength->SetValue(td.GetAllowedLength());

	const bool unlimited = td.GetLength() == 0;
	m_unlimited->SetValue(unlimited);
	m_length->Enable(!unlimited);
	m_allowedLength->Enable(!unlimited);

	ibPGTypeProperty::SetExpanded(true);
}

// THE VALUE'S PICTURE — the one the chosen type's own choice carries; none for several types at once. This
// property answers no choice selection (GetChoiceSelection), so the grid would draw none of its own accord.
wxBitmapBundle ibPGTypeProperty::GetValuePicture() const
{
	const ibTypeDescription td = ReadType(m_value);
	if (td.GetClsidCount() != 1)
		return wxBitmapBundle();
	const ibClassID chosen = td.GetFirstClsid();
	for (const std::pair<const int, ibClassID>& choice : m_valChoices) {
		if (choice.second != chosen)
			continue;
		const int idx = m_choices.Index(choice.first);
		return idx != wxNOT_FOUND ? m_choices.Item(idx).GetBitmap() : wxBitmapBundle();
	}
	return wxBitmapBundle();
}

wxSize ibPGTypeProperty::OnMeasureImage(int /*item*/) const
{
	return ibMeasureValuePicture(GetValuePicture(), GetGrid());
}

void ibPGTypeProperty::OnCustomPaint(wxDC& dc, const wxRect& rect, ibPGPaintData& paintdata)
{
	ibPaintValuePicture(GetValuePicture(), dc, rect, paintdata);
}

#include "frmclient/win/dlgs/typeSelector.h"   // the shared picker — this editor is one of its two callers

ibPGEditorDialogAdapter* ibPGTypeProperty::GetEditorDialog() const
{
	class ibPGEditorTypeDialogAdapter : public ibPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) wxOVERRIDE
		{
			ibPGTypeProperty* dlgProp = wxDynamicCast(prop, ibPGTypeProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibVariant value = dlgProp->GetValue();

			// WHAT IS ON OFFER is the property's, as the server offered it — the desktop asked the configuration for it by
			// the shape (GetTypesByFilter); so is SINGLE OR COMPOSITE: a table slot takes one type, everything else may be
			// composite.
			const bool singleChoice = !dlgProp->IsMultiple();

			ibTypeDescription typeDesc = ReadType(value);
			if (!ibShowTypeSelector(pg, ibSelectorDataType::ibSelectorDataType_any, dlgProp->GetTypeList(), typeDesc,
				nullptr, !dlgProp->HasFlag(ibPGFlags::ReadOnly), singleChoice))
				return false;

			SetValue(WriteType(value, typeDesc));
			return true;
		}
	};

	return new ibPGEditorTypeDialogAdapter();
}
