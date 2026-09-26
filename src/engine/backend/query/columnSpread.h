#ifndef __COLUMN_SPREAD_H__
#define __COLUMN_SPREAD_H__

// Shared physical-spread machinery — the role-driven binding both codecs build on: the L3-1 VALUE
// codec (ibColumnCodec::WriteValue, columnLayout.cpp — source = ibValue) and the L3-3 WIRE codec
// (ibDataMover::BinaryToStatement, dataMover.cpp — source = the binary wire). Both bind a column's
// _TYPE tag + per-present-primitive value + reference pair into an ibQueryStatement in the ONE
// load-bearing order; only the value SOURCE differs, so the per-slot bind is a caller callback.
// Internal to the codec TUs — not part of any public surface.

#include "backend/query/columnLayout.h"   // ibColumnRole / ibPersistedTypeTag / ibColumnCodec::HasReference / ibQueryStatement
#include "backend/backend_core.h"          // emptyDate
#include "backend/compiler/value.h"        // ibValueTypes
#include "backend/system/value/valueJob.h"    // g_valueScheduleCLSID — a value object stored whole
#include "backend/system/value/valueType.h"   // g_valueTypeDescriptionCLSID — the other one

namespace ibColumnSpread {

// The persisted _TYPE tag an ibValue stores — the inverse of the read switch in ReadValue.
// EMPTY / NULL carry their own tags; anything that is not a primitive is a Reference.
inline ibFieldTypes TagForValueType(ibValueTypes vt)
{
	switch (vt) {
	case ibValueTypes::TYPE_EMPTY:   return ibFieldTypes_Empty;
	case ibValueTypes::TYPE_BOOLEAN: return ibFieldTypes_Boolean;
	case ibValueTypes::TYPE_NUMBER:  return ibFieldTypes_Number;
	case ibValueTypes::TYPE_DATE:    return ibFieldTypes_Date;
	case ibValueTypes::TYPE_STRING:  return ibFieldTypes_String;
	case ibValueTypes::TYPE_NULL:    return ibFieldTypes_Null;
	case ibValueTypes::TYPE_ENUM:    return ibFieldTypes_Enum;
	default:                         return ibFieldTypes_Reference;
	}
}

// The tag an ibVALUE stores — the type answers for every primitive, and the CLSID answers for the
// one value object a column stores whole. Asking the class first is what keeps a schedule from
// being written as a reference: TYPE_VALUE falls into the default arm above, which is "not a
// primitive, therefore a reference", and a schedule is neither.
inline ibFieldTypes TagForValue(const ibValue& value)
{
	if (value.GetClassType() == g_valueScheduleCLSID)
		return ibFieldTypes_Schedule;
	// The second value object stored whole — a type description (a characteristic's own Type). Same
	// reason it is asked by CLSID before the type switch: TYPE_VALUE would otherwise fall into the
	// "not a primitive, therefore a reference" arm, and it is neither.
	if (value.GetClassType() == g_valueTypeDescriptionCLSID)
		return ibFieldTypes_TypeDescription;
	// ⭐ AN ENUMERATION IS ONE BY ITS CLSID KIND, not by which of its two shapes it happens to be in.
	// A chosen member is an ibValueEnumerationVariant (TYPE_ENUM); an unset one is the enumeration
	// OWNER, whose value type is TYPE_VALUE — so it fell into the "not a primitive, therefore a
	// reference" arm and was persisted with the Reference tag. The column has no _RTRef / _RRRef pair
	// to read that back from (an enum spreads to _TYPE + _E), so the next list read asked the driver
	// for a field that does not exist and the whole page died. Ask the KIND, and answer with what the
	// cell actually holds: nothing, or a member.
	if (IsEnum(value.GetClassType()))
		return value.IsEmpty() ? ibFieldTypes_Empty : ibFieldTypes_Enum;

	// 🛑 AND AN EMPTY REFERENCE IS **NOT** FOLDED INTO `Empty`, though it looks like the same rule one line
	// up. An empty reference is OF A TYPE — "a Catalogue.Goods, but no particular one" — and the Empty tag
	// says something else: that nothing was chosen at all. On a composite column those are two different
	// answers, and the whole of the narrowing arc rests on being able to tell them apart (Max, 2026-09-24:
	// "an empty reference can be of a concrete type; an empty reference and an undefined that was written
	// are different values"). An enumeration folds because its unset shape carries no target to remember;
	// a reference does.
	return TagForValueType(value.GetType());
}

// Bind a primitive slot's PLACEHOLDER — the value it carries when it is NOT the active type
// (false / 0 / emptyDate / "" / wxNOT_FOUND). Keyed by role, one place for the constants the old
// hand-rolled spreads repeated per branch.
inline void BindAbsentPrimitive(ibQueryStatement* st, ibColumnRole role, int& pos)
{
	switch (role) {
	case ibColumnRole::Boolean: st->SetParamBool(pos++, false);             break;
	case ibColumnRole::Number:  st->SetParamNumber(pos++, 0);               break;
	case ibColumnRole::Date:    st->SetParamDate(pos++, emptyDate);         break;
	case ibColumnRole::String:  st->SetParamString(pos++, wxEmptyString);   break;
	case ibColumnRole::Enum:    st->SetParamInt(pos++, emptyEnum);          break;
	default:                                                                break;
	}
}

// The ONE physical-spread driver. Walks the column's roles in the LOAD-BEARING order — identical to
// DescribeColumnLayout (kept in lock-step; that order is the keyset anchor) — binding the _TYPE tag,
// then for each PRESENT primitive the ACTIVE value (via bindActive) when its role matches `tag`, else
// the absent placeholder, then the reference pair (via bindRef). No slot vector is materialised: this
// runs on a per-cell write hot path, so it stays allocation-free — which slots the column has is
// answered in one pass over its classes, each answer the one DescribeColumnLayout gets from ContainType
// and its reference test, so the field SET and ORDER are byte-identical to the layout (and thus to the DDL).
template <class BindActive, class BindRef>
void DriveSpread(const ibBackendQueryColumn* col, int tag,
                 ibQueryStatement* st, int& pos, BindActive bindActive, BindRef bindRef)
{
	// The SAME description the layout used — what a value may BE, characteristic redirection included.
	// This driver has to agree with DescribeColumnLayout slot for slot: the INSERT's column list comes
	// from the layout, the parameters come from here, and a slot one of them knows about and the other
	// does not shifts every parameter after it one position early (see the note further down).
	const ibTypeDescription& td = col->GetTypeValueDesc();
	st->SetParamInt(pos++, tag);   // _TYPE discriminator

	// WHICH SLOTS THE COLUMN HAS, in ONE pass over the classes its type admits — each answer exactly the one
	// DescribeColumnLayout gets from ContainType / the reference test, so the two stay slot for slot. Asked as
	// seven searches of the list per cell, this was a share of writing a register line of its own (stack
	// samples 2026-09-14, Debug).
	bool hasBoolean = false, hasNumber = false, hasDate = false, hasString = false, hasEnum = false;
	bool hasSchedule = false, hasTypeDescription = false, hasReference = false;
	for (const ibClassID& clsid : td.GetClsidList()) {
		if      (clsid == g_valueBooleanCLSID)         hasBoolean = true;
		else if (clsid == g_valueNumberCLSID)          hasNumber = true;
		else if (clsid == g_valueDateCLSID)            hasDate = true;
		else if (clsid == g_valueStringCLSID)          hasString = true;
		else if (clsid == g_valueScheduleCLSID)        hasSchedule = true;
		else if (clsid == g_valueTypeDescriptionCLSID) hasTypeDescription = true;
		else if (IsReference(clsid))                   hasReference = true;
		else if (IsEnum(clsid) && ibValue::IsRegisterCtor(clsid)) hasEnum = true;
	}

	auto primitive = [&](bool present, ibColumnRole role) {
		if (!present)
			return;
		if (ibPersistedTypeTag(role) == tag) bindActive(role, pos);
		else                                 BindAbsentPrimitive(st, role, pos);
	};
	primitive(hasBoolean, ibColumnRole::Boolean);
	primitive(hasNumber,  ibColumnRole::Number);
	primitive(hasDate,    ibColumnRole::Date);
	primitive(hasString,  ibColumnRole::String);
	primitive(hasEnum,    ibColumnRole::Enum);

	// The schedule slot sits between the primitives and the reference pair — the same place
	// DescribeColumnLayout puts it, and that order is the keyset anchor, so the two must be read
	// as one thing. Its "absent" form is an empty blob, bound by the caller: a NULL there would
	// read back as a corrupt schedule rather than as no schedule.
	if (hasSchedule) {
		if (tag == ibFieldTypes_Schedule) bindActive(ibColumnRole::Schedule, pos);
		else                              st->SetParamNull(pos++);
	}

	// The type-description slot, on the same footing and in the same place as the schedule. It is
	// not decoration: this driver is the ONE thing that has to agree, slot for slot, with
	// DescribeColumnLayout — the column list of the INSERT comes from the layout, the parameters
	// come from here. A slot the layout emits and the driver skips does not bind a NULL, it binds
	// EVERYTHING AFTER IT ONE POSITION EARLY, and the last parameter of the statement runs off the
	// end of the prepared buffer. That is what the driver reported as a null parameter buffer.
	if (hasTypeDescription) {
		if (tag == ibFieldTypes_TypeDescription) bindActive(ibColumnRole::TypeDescription, pos);
		else                                     st->SetParamNull(pos++);
	}

	if (hasReference) {
		bindRef(ibColumnRole::ReferenceType, pos);
		bindRef(ibColumnRole::ReferenceId,   pos);
	}
}

} // namespace ibColumnSpread

#endif // !__COLUMN_SPREAD_H__
