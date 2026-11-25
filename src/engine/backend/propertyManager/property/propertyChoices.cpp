#include "propertyChoices.h"
#include "backend/propertyManager/property/variant/variantChoices.h"

ibVariant ibPropertyChoices::CreateVariantData(ibPropertyObject* property, const ibChoicesDescription& choicesDesc)
{
	return ibVariant(new ibVariantChoices(property, choicesDesc));
}

ibChoicesDescription& ibPropertyChoices::GetValueAsChoicesDesc() const
{
	return get_cell_variant<ibVariantChoices>()->GetChoicesDesc();
}

// The family rule — see propertyRecord.cpp.
void ibPropertyChoices::DoSetValue(const ibVariant& val)
{
	if (const ibVariantChoices* carried = find_cell_variant<ibVariantChoices>(val)) {
		SetValue(carried->GetChoicesDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

bool ibPropertyChoices::ReadNodeValue(const ibDataValue& value)
{
	return ibChoicesDescriptionMemory::ReadNode(value, GetValueAsChoicesDesc());
}

bool ibPropertyChoices::WriteNodeValue(ibDataValue& value) const
{
	return ibChoicesDescriptionMemory::WriteNode(value, GetValueAsChoicesDesc());
}

////////////////////////////////////////////////////////////////////////

#include "protocol/protocolStudio.h"         // ibProtocol::ibSchema::ibProperties — the row's keys
#include "core/serialize/dataBuilder.h"
#include "backend/backend_type.h"              // ibBackendTypeFactory — the field the switch stands in; its type's words
#include "backend/metaData.h"                  // CreateObject — a value of the type, asked for its values
#include "backend/metadataSerialization.h"     // ibSerializeValue — a value as a row stores it
#include "backend/metaCollection/metaObject.h" // g_metaEnumerationCLSID — an enumeration's reference, by its metaclass
#include "backend/backend_exception.h"         // ibCoreException — a value of a type the application lacks
#include "backend/backend_localization.h"      // ibTranslateString — a presentation, in the language in force
#include "backend/system/value/valueList.h"    // ibValueValueList — the items as a script reads and writes them

#include <algorithm>                           // std::find_if — an item's translations, kept for the same value

namespace {

// A type whose values can be listed: a Boolean, an enumeration — of the application or of the engine. Not a reference
// to a catalog: its values are the base's, and a switch is not a list form (a reference is a later phase).
bool IsFinite(const ibClassID& clsid)
{
	return clsid == g_valueBooleanCLSID || ::IsEnum(clsid)
		|| (::IsReference(clsid) && clsid_metaclass(clsid) == clsid_metaclass(g_metaEnumerationCLSID));
}

// The values a finite type lists — the short list a value of it is picked from (ibValue::FindValue with no text), what
// the quick choice offers. None for an open type.
std::vector<ibValue> ValuesOf(const ibTypeDescription& typeDesc, const ibMetaData* metaData)
{
	std::vector<ibValue> values;
	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!IsFinite(clsid))
			continue;
		const ibValue made = metaData->CreateObject(clsid);
		std::vector<ibValue> found;
		if (made.GetType() == ibValueTypes::TYPE_EMPTY || !made.FindValue(ibEmptyString, ibChoiceCondition(), found))
			continue;
		values.insert(values.end(), found.begin(), found.end());
	}
	return values;
}

// THE ITEMS ARE OF THE FIELD'S TYPE (Max, 2026-11-23): a list whose every value is of the type stays; one left from a
// type the field no longer has is cleared — "it was a string, it became a Boolean: they are simply cleared". The switch
// then says it is empty, and its window fills it anew. Nothing to judge by (not on a field yet, no type) — as it came.
//
// ⭐ …AND HELD TO ITS QUALIFIERS, as the value an item writes is (AdjustValue: a number's digits, a string's length) — here,
// where every road ends: the window, a script's value list, MCP. Past them, an item picked wrote another value than its
// own, and the switch then marked none: 3.257 on a field of two digits wrote 3.26 (2026-11-24, a script's value list).
ibChoicesDescription OfFieldType(const ibChoicesDescription& choices, const ibPropertyObject* owner)
{
	// The field the switch stands in holds the type: the switch is its view, attached to it (ibFieldViewProvider).
	const ibBackendTypeFactory* const typed = owner != nullptr ? owner->GetAttachOwner<ibBackendTypeFactory>() : nullptr;
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	if (typed == nullptr || metaData == nullptr || typed->GetTypeDesc().GetClsidList().empty())
		return choices;

	const ibTypeDescription& typeDesc = typed->GetTypeDesc();
	ibChoicesDescription held;
	for (const ibChoiceDescription& row : choices.m_rows) {
		ibValue value;
		try {
			value = ibDeserializeValue(row.m_value, metaData);
		}
		catch (const ibCoreException&) {
			return ibChoicesDescription();   // a type this application no longer has
		}
		if (!typeDesc.ContainType(value.GetClassType()))
			return ibChoicesDescription();

		ibChoiceDescription item;
		ibSerializeValue(typed->AdjustValue(value), item.m_value);
		item.m_presentation = row.m_presentation;
		held.m_rows.push_back(std::move(item));
	}
	return held;
}

} // namespace

void ibPropertyChoices::SetValue(const ibChoicesDescription& val)
{
	m_propValue = CreateVariantData(m_owner, OfFieldType(val, m_owner));
}

bool ibPropertyChoices::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;

	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	const ibBackendTypeFactory* const typed = owner != nullptr ? owner->GetAttachOwner<ibBackendTypeFactory>() : nullptr;
	if (typed == nullptr)
		return true;

	const ibTypeDescription& typeDesc = typed->GetTypeDesc();
	ibDataValue type;
	ibBackendTypeDescriptionMemory::WriteNode(type, typeDesc, metaData);
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Type, type);

	auto values = std::make_shared<ibDataNode>();
	if (metaData != nullptr) {
		for (const ibValue& value : ValuesOf(typeDesc, metaData)) {
			ibDataNode& item = values->AddChild(0, static_cast<ibMetaID>(values->Children().size() + 1));
			ibSerializeValue(value, item.Child(ibProtocol::ibSchema::ibProperties::Value));
			item.SetValue(ibProtocol::ibSchema::ibProperties::Presentation, value.GetString());
		}
	}
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Values, ibDataValue::Child(values));
	return true;
}

// ⭐ A SCRIPT READS THE ITEMS AS A VALUE LIST (Max, 2026-11-24: "it returns the list of values") — each its value and
// the text it shows in the session's language, one with no text of its own its value's. A copy: the switch changes
// when a list is written back. An item of a type the application no longer has is not in it.
bool ibPropertyChoices::GetDataValue(ibValue& pvarPropVal) const
{
	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;

	ibValuePtr<ibValueValueList> list(new ibValueValueList());
	for (const ibChoiceDescription& row : GetValueAsChoicesDesc().m_rows) {
		try {
			list->Add(ibDeserializeValue(row.m_value, metaData), ibTranslateString(row.m_presentation).GetString(), false, ibValue());
		}
		catch (const ibCoreException&) {
		}
	}
	pvarPropVal = list;
	return true;
}

// …AND A LIST WRITTEN IS THE ITEMS, in its order: its values, of the field's type (SetValue clears a list holding one
// of another, as the window's), and their texts. A text that is the value's own is kept as none — the switch shows the
// value's in any language; one an item already shows for the same value keeps that item's translations. Anything but
// a value list is refused.
bool ibPropertyChoices::SetDataValue(const ibValue& varPropVal)
{
	ibValueValueList* list = nullptr;
	if (!varPropVal.ConvertToValue(list))
		return false;

	const ibChoicesDescription& was = GetValueAsChoicesDesc();
	ibChoicesDescription choices;
	for (long line = 0; line < list->GetRowCount(); line++) {
		const ibValue value = list->Storage().GetCell(line, ibValueValueList::eValue);
		const ibString text = list->Storage().GetCell(line, ibValueValueList::ePresentation).GetString();
		ibChoiceDescription row;
		ibSerializeValue(value, row.m_value);
		if (text != value.GetString()) {
			const auto same = std::find_if(was.m_rows.begin(), was.m_rows.end(), [&row, &text](const ibChoiceDescription& item) {
				return item.m_value == row.m_value && ibTranslateString(item.m_presentation).GetString() == text;
			});
			row.m_presentation = same != was.m_rows.end() ? same->m_presentation : text;
		}
		choices.m_rows.push_back(std::move(row));
	}
	SetValue(choices);
	return true;
}

ibString ibChoiceCaption(const ibChoiceDescription& row, const ibMetaData* metaData)
{
	const ibString presentation = ibTranslateString(row.m_presentation).GetString();
	if (!presentation.IsEmpty())
		return presentation;

	// …and an item with neither says so: a button with no text is a blank, not an empty value (Max, 2026-11-23).
	try {
		const ibString text = ibDeserializeValue(row.m_value, metaData).GetString();
		return !text.IsEmpty() ? text : ibString(_("<empty>"));
	}
	catch (const ibCoreException&) {
		return _("<gone>");   // a type this application no longer has — as a choice parameter's row says it
	}
}

bool ibIsChoiceType(const ibClassID& clsid)
{
	return clsid == g_valueNumberCLSID || clsid == g_valueStringCLSID || clsid == g_valueDateCLSID || IsFinite(clsid);
}

// The items as the switch shows them, the first three — one line, a long list shortened rather than hidden, as the
// choice parameters' row (ibVariantChoiceParameters::GetString).
ibString ibVariantChoices::GetString() const
{
	if (!m_choicesDesc.IsOk())
		return _("<empty>");   // none set says so, as the switch shows it

	const ibMetaData* const metaData = m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr;

	ibString shown;
	unsigned int named = 0;
	for (const ibChoiceDescription& row : m_choicesDesc.m_rows) {
		if (named == 3) {
			shown += ibT(", ...");
			break;
		}
		if (named > 0)
			shown += ibT(", ");
		shown += ibChoiceCaption(row, metaData);
		named++;
	}
	return shown;
}
