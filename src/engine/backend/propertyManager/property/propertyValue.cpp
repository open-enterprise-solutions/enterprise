#include "propertyValue.h"
#include "propertySource.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — a value that is no scalar, as its node
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"        // ibValueMetaObjectRecordDataEnumRef — its members
#include "backend/metaCollection/partial/reference/reference.h" // ibValueReferenceDataObject — a member as a value
#include "backend/system/value/valueType.h"                     // ibValueTypeDescription::AdjustValue
#include "backend/backend_exception.h"                          // ibBackendCoreException — a value that cannot be read
#include "protocol/protocol.h"                                   // ibProtocol::ibSchema::ibProperties::Id — a choice by its id

namespace {

// THE ONE TYPE A VALUE IS CHOSEN AMONG — the source's, when it holds exactly one: none for a composite, which is
// typed, not chosen.
ibClassID SingleClass(const ibPropertySource* typedBy)
{
	if (typedBy == nullptr || typedBy->IsEmptyProperty())
		return 0;
	const ibTypeDescription& type = typedBy->GetValueAsTypeDesc();
	return type.GetClsidCount() == 1 ? type.GetFirstClsid() : 0;
}

}

ibValue ibPropertyValue::GetValueAsValue() const
{
	return ibValue(m_propValue);
}

void ibPropertyValue::SetValue(const ibValue& value)
{
	m_propValue = value;
}

ibPropertyChoiceMode ibPropertyValue::GetValueList(ibPropertyChoiceList& list)
{
	const ibClassID clsid = SingleClass(m_typedBy);
	if (clsid == 0)
		return ibPropertyChoiceMode::None;

	if (clsid == g_valueBooleanCLSID) {
		list.Add(1, ibT("True"), _("True"), ibVariant(true));
		list.Add(0, ibT("False"), _("False"), ibVariant(false));
		return ibPropertyChoiceMode::Single;
	}

	// An enumeration's members, each as the value a field of its type holds — its reference.
	const ibMetaData* const metaData = GetPropertyObject() != nullptr ? GetPropertyObject()->GetMetaData() : nullptr;
	const ibCtorMetaValueType* const ctor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	const ibValueMetaObjectRecordDataEnumRef* const enumeration = ctor != nullptr
		? dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(ctor->GetMetaObject()) : nullptr;
	if (enumeration == nullptr)
		return ibPropertyChoiceMode::None;

	for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray()) {
		const ibValue value = ibValueReferenceDataObject::Create(enumeration, member->GetGuid());
		list.Add(member->GetMetaID(), member->GetName(), member->GetSynonym(), value);
	}
	return ibPropertyChoiceMode::Single;
}

bool ibPropertyValue::SetDataValue(const ibValue& varPropVal)
{
	SetValue(varPropVal);
	return true;
}

bool ibPropertyValue::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = GetValueAsValue();
	return true;
}

bool ibPropertyValue::ReadNodeValue(const ibDataValue& value)
{
	const ibMetaData* const metaData = GetPropertyObject() != nullptr ? GetPropertyObject()->GetMetaData() : nullptr;

	// A value that is no scalar — a member of an enumeration, a reference — comes as its node, through the door it was
	// written by.
	if (value.Kind() == ibDataKind::Child) {
		// ⚠ A VALUE THAT CANNOT BE READ RAISES — a member of an enumeration deleted since, a type nobody has. The
		// refusal says what was missing; the owner says whose value it was (ibValueRadioButton::ReadData).
		const std::optional<ibDataNode> node = value.AsChild();
		if (!node)
			return false;
		// …or ONE OF ITS CHOICES NAMED BY ITS ID, as the inspector's drop-down names it (its Id, no type of a value):
		// a scalar would not do, a member held being a node (ibBackendProperty::SetNodeValue keeps a node a node).
		if (node->FindField(ibProtocol::ibSchema::ibProperties::Id)) {
			const long id = node->GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id);
			ibPropertyChoiceList choices;
			GetValueList(choices);
			for (unsigned int idx = 0; idx < choices.GetCount(); ++idx) {
				if (choices.GetId(idx) == id) {
					SetValue(ibValue(choices.GetValue(idx)));
					return true;
				}
			}
			ibBackendCoreException::Error(_("There is no value %d among the ones it may take"), static_cast<int>(id));
		}
		SetValue(metaData != nullptr ? metaData->Deserialize(*node) : ibValue::FromNode(*node));
		return true;
	}

	ibValue scalar;
	switch (value.Kind()) {
	case ibDataKind::Bool: scalar = value.AsBool(); break;
	case ibDataKind::Number: scalar = value.AsNumber(); break;
	case ibDataKind::String: scalar = value.AsString(); break;
	case ibDataKind::Date: scalar = value.AsDate(); break;
	default: return false;
	}

	// An empty text is no value: the button stands for none.
	if (scalar.GetType() == ibValueTypes::TYPE_STRING && scalar.GetString().IsEmpty()) {
		SetValue(ibValue());
		return true;
	}

	// ⭐ A CHOSEN TYPE TAKES ONE OF ITS CHOICES — by its name or its label, as a caller writes one; a word that names
	// none is a refusal, never a string standing in a boolean's place.
	ibPropertyChoiceList choices;
	if (GetValueList(choices) != ibPropertyChoiceMode::None) {
		for (unsigned int idx = 0; idx < choices.GetCount(); ++idx) {
			const ibValue choice(choices.GetValue(idx));
			const bool same = scalar.GetType() == ibValueTypes::TYPE_STRING
				? scalar.GetString().IsSameAs(choices.GetName(idx), false) || scalar.GetString().IsSameAs(choices.GetLabel(idx), false)
				: scalar == choice;
			if (same) {
				SetValue(choice);
				return true;
			}
		}
		ibString allowed;
		for (unsigned int idx = 0; idx < choices.GetCount(); ++idx)
			allowed << (idx > 0 ? ibT(", ") : ibT("")) << choices.GetName(idx);
		ibBackendCoreException::Error(_("'%s' is none of the values it may take: %s"), scalar.GetString(), allowed);
	}

	// Any other type takes the scalar as a field of it would: a "5" for a number is 5. Without a source there is no
	// type to take it as, and it stays what it is.
	if (m_typedBy != nullptr && !m_typedBy->IsEmptyProperty()) {
		const ibTypeDescription& type = m_typedBy->GetValueAsTypeDesc();
		if (type.GetClsidCount() > 0 && !type.ContainType(scalar.GetClassType()))
			scalar = ibValueTypeDescription::AdjustValue(type, scalar, metaData);
	}
	SetValue(scalar);
	return true;
}

bool ibPropertyValue::WriteNodeValue(ibDataValue& value) const
{
	if (IsEmptyProperty())
		return false;

	const ibValue held = GetValueAsValue();
	switch (held.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: value = ibDataValue::Bool(held.GetBoolean()); return true;
	case ibValueTypes::TYPE_NUMBER: value = ibDataValue::Number(held.GetNumber()); return true;
	case ibValueTypes::TYPE_STRING: value = ibDataValue::String(held.GetString()); return true;
	case ibValueTypes::TYPE_DATE: value = ibDataValue::Date(held.GetDate()); return true;
	default: break;
	}

	const ibMetaData* const metaData = GetPropertyObject() != nullptr ? GetPropertyObject()->GetMetaData() : nullptr;
	auto node = std::make_shared<ibDataNode>();
	try {
		if (metaData != nullptr)
			metaData->Serialize(held, *node);
		else if (!held.Serialize(*node))
			return false;
	}
	catch (const ibCoreException&) {
		return false;
	}
	value = ibDataValue::Child(node);
	return true;
}
