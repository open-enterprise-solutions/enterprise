#include "backend/query/columnLayout.h"

// What a field may never be compared on — computed or composite values, no column of their own:
#include "backend/system/value/valueType.h"                        // g_valueTypeDescriptionCLSID
#include "backend/system/value/valueJob.h"                         // g_valueScheduleCLSID
#include "backend/system/value/valuePointInTime.h"                 // g_valuePointInTimeCLSID
// (The value codec that fills these fields — ibColumnCodec — is provider/database/columnCodec.cpp: it assembles
//  values with the metadata, which in query/ is the provider's alone.)

// The single role -> suffix table (see columnLayout.h). Built once; an unknown role
// (only Raw) yields the empty suffix.
// ⚠ A SWITCH, NOT A MAP: every field of every cell is spelled through here, and a tree lookup per
// spelling stood in the stack samples of a report's read (2026-09-12).
const ibString& ibFieldSuffix(ibColumnRole role)
{
	static const ibString s_empty;
	static const ibString s_type(ibT("_TYPE")), s_bool(ibT("_B")), s_number(ibT("_N")), s_date(ibT("_D")),
		s_string(ibT("_S")), s_enum(ibT("_E")), s_refType(ibT("_RTRef")), s_refId(ibT("_RRRef")),
		s_schedule(ibT("_SCH")), s_typeDesc(ibT("_TD")), s_value(ibT("_V"));
	switch (role) {
	case ibColumnRole::Discriminator:   return s_type;
	case ibColumnRole::Boolean:         return s_bool;
	case ibColumnRole::Number:          return s_number;
	case ibColumnRole::Date:            return s_date;
	case ibColumnRole::String:          return s_string;
	case ibColumnRole::Enum:            return s_enum;
	case ibColumnRole::ReferenceType:   return s_refType;
	case ibColumnRole::ReferenceId:     return s_refId;
	case ibColumnRole::Schedule:        return s_schedule;
	case ibColumnRole::TypeDescription: return s_typeDesc;
	case ibColumnRole::Value:           return s_value;
	default:                            return s_empty;   // Raw — the column is its own field
	}
}

ibString ibSqlAliasOf(const ibString& outputName)
{
	// THE PREFIX IS A NAMESPACE, NOT AN ESCAPE.
	//
	// These names are written INSIDE a projection of ours — a nested select, a declared query's body
	// — so they are ours to spell, all of them, and they are spelled in that projection's own
	// namespace. The prefix says where the name lives. That a prefixed word can no longer collide
	// with SQL's vocabulary is a CONSEQUENCE of saying so, not the purpose.
	//
	// 🛑 THE FIRST VERSION TESTED A WORD LIST and prefixed only on a hit. Two things were wrong with
	// it, and the second is the serious one:
	//   · the list is a guess — it has to be right for four dialects and stay right as each adds
	//     keywords, and nothing tells you it is incomplete;
	//   · a name that changed only for `Date` meant the writer/reader split was exercised almost
	//     never, so a reader that forgot to ask here would work for every query but one and fail
	//     SILENTLY there — an empty value, not a refusal. Prefixing everything makes the same
	//     mistake break loudly, on the first query anyone runs.
	//
	// ⭐ ONE SPELLING FOR EVERY NAME: `out_` and the FNV-1a hash of the author's whole name, Latin or not
	// (a Cyrillic alias is a token Firebird does not know, and quoting is off). Stable, so the writer and
	// the reader agree by computing it rather than by passing the spelling between them. Distinct outputs
	// stay distinct as far as a 32-bit hash keeps them apart.
	// An unnamed output has nothing to qualify — it is read by position, not by name.
	if (outputName.IsEmpty())
		return outputName;

	// 🛑 …AND IT HAS TO COME BACK. A result is read by its labels, and the Firebird driver keeps 31 characters
	// of a label (XSQLDA): `out_CurrencyAmountCorrTurnoverCr` is 32, came back cut, was found by no reader and
	// read as an EMPTY sum (measured 2026-09-16, the corresponding turnovers of the ledger). The label is
	// twelve characters whatever the name.
	//
	// ⚠ A FIELD OF AN OUTPUT IS THE OUTPUT'S LABEL AND THE FIELD'S SUFFIX (`<label>_RTRef`, `<label>_RRRef`),
	// composed by whoever spreads it or reads it (dbTableProvider, `Column(prefix, col)`) — never asked here as
	// a name of its own: `AccountDimension1_TYPE` hashed whole is a label no reader asks for (2026-09-17).
	unsigned long hash = 2166136261UL;
	for (const ibChar ch : outputName) {
		hash ^= static_cast<unsigned long>(ch);
		hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
	}
	return ibString::Format(ibT("out_%08lx"), hash);
}

bool ibIsPlainScalarType(const ibTypeDescription& type)
{
	return type.GetClsidCount() == 1
		&& (type.ContainType(ibValueTypes::TYPE_NUMBER) || type.ContainType(ibValueTypes::TYPE_STRING)
		    || type.ContainType(ibValueTypes::TYPE_DATE) || type.ContainType(ibValueTypes::TYPE_BOOLEAN));
}

bool ibReadsBackAsItself(const ibTypeDescription& type)
{
	return ibIsPlainScalarType(type) && !type.ContainType(ibValueTypes::TYPE_BOOLEAN);
}

const ibString& ibOwnerRefField()
{
	// Built from the ONE suffix table rather than written out: this column and a reference key hold
	// the same sixteen bytes, and they stay spelled alike because they are spelled from one place.
	static const ibString s_field = ibT("Row") + ibFieldSuffix(ibColumnRole::ReferenceId);
	return s_field;
}

std::shared_ptr<const ibBackendColumnRawDB> ibOwnerRefColumn()
{
	return ibBackendColumnRawDB::Guid(ibOwnerRefField());
}


int ibPersistedTypeTag(ibColumnRole role)
{
	switch (role) {
	case ibColumnRole::Boolean:       return ibFieldTypes_Boolean;
	case ibColumnRole::Number:        return ibFieldTypes_Number;
	case ibColumnRole::Date:          return ibFieldTypes_Date;
	case ibColumnRole::String:        return ibFieldTypes_String;
	case ibColumnRole::Enum:          return ibFieldTypes_Enum;
	case ibColumnRole::ReferenceType: return ibFieldTypes_Reference;
	case ibColumnRole::ReferenceId:   return ibFieldTypes_Reference;
	case ibColumnRole::Schedule:      return ibFieldTypes_Schedule;
	case ibColumnRole::TypeDescription: return ibFieldTypes_TypeDescription;
	case ibColumnRole::Value:         return ibFieldTypes_Value;
	default:                          return ibFieldTypes_Empty;   // Raw / Discriminator carry no tag
	}
}


// Kept here because this is where the layout decides what a column BECOMES physically — so the
// storage fact and the "can it be compared" fact cannot drift apart.
//
//   STORED WHOLE — a schedule, a type description: one BLOB field, and SQL compares no blobs.
//   NOT STORED AT ALL — the MOMENT (PointInTime): computed from the date and the reference, with no
//   field of its own, so there is nothing to compare either. It is in the field tree so a QUERY can
//   address it, and a query that WRITES its fields asks the column instead (Kind::Synthetic).
bool ibIsComparableType(const ibTypeDescription& typeDesc)
{
	return !typeDesc.ContainType(g_valueScheduleCLSID)
	    && !typeDesc.ContainType(g_valueTypeDescriptionCLSID)
	    && !typeDesc.ContainType(g_valuePointInTimeCLSID);
}

// THE AUTHORITY, and it now asks the COLUMN — which answers with the derivation below unless it has
// a reason not to. Kept as a free function because that is how every tier already spells the
// question; what changed is who owns the answer.
std::vector<ibColumnSlot> DescribeColumnLayout(const ibBackendQueryColumn& col)
{
	return col.DescribeLayout();
}

// (THE DEFAULT DERIVATION lives in queryColumn.cpp, with the other things a column DOES — together
//  with the two helpers only it used (the primitive slot's canonical type, and a raw column's carried
//  type). This file keeps the layout VOCABULARY and the value codec.)

// A column's VALUE fields — its layout's value-role slots (every role EXCEPT the _TYPE discriminator
// and the reference TYPE id). The ONE metadata-free authority for "which fields carry this column's
// value", derived over DescribeColumnLayout — it replaces the former per-class GetValueFields
// overrides (raw -> its one field; an attribute / temp column -> its composite spread): the role
// model already encodes each column's storage shape. A free TIER helper, not a column method — only
// the DB provider asks it (sort / group-by / key field expansion); the column stays a pure descriptor.
bool ibIsValueRole(ibColumnRole role)
{
	return role != ibColumnRole::Discriminator && role != ibColumnRole::ReferenceType;
}

std::vector<ibColumnSlot> ColumnValueSlots(const ibBackendQueryColumn& col)
{
	std::vector<ibColumnSlot> out;
	for (ibColumnSlot& slot : DescribeColumnLayout(col))
		if (ibIsValueRole(slot.m_role))
			out.push_back(std::move(slot));
	return out;
}

ibColumnSlot FirstValueSlot(const ibBackendQueryColumn& col)
{
	std::vector<ibColumnSlot> slots = ColumnValueSlots(col);
	return slots.empty() ? ibColumnSlot() : std::move(slots.front());
}

std::vector<ibString> ColumnValueFields(const ibBackendQueryColumn& col)
{
	std::vector<ibString> out;
	for (const ibColumnSlot& slot : ColumnValueSlots(col))
		out.push_back(slot.m_name);
	return out;
}

std::vector<ibString> ColumnSortFields(const ibBackendQueryColumn& col)
{
	std::vector<ibString> out = ColumnValueFields(col);
	if (FirstValueSlot(col).m_role == ibColumnRole::ReferenceId)
		out.push_back(col.GetPhysicalName() + ibFieldSuffix(ibColumnRole::ReferenceType));
	return out;
}

// ==========================================================================
// SQL fragment builders — the field spellings the hand-written query / upsert SQL used to
// assemble per-attribute, now derived once from the layout (same field SET + ORDER as the codec).
// ==========================================================================

bool ibSameFieldType(const ibColumnType& a, const ibColumnType& b)
{
	return a.m_kind == b.m_kind && a.m_length == b.m_length && a.m_precision == b.m_precision
	    && a.m_scale == b.m_scale && a.m_datePrec == b.m_datePrec && a.m_fixed == b.m_fixed;
}

std::vector<ibString> ColumnFieldNames(const ibBackendQueryColumn& col)
{
	std::vector<ibString> out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col))
		out.push_back(slot.m_name);
	return out;
}

ibString ColumnFieldList(const ibBackendQueryColumn& col, const ibString& aggr)
{
	ibString out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		// The numeric / date MEASURES carry the aggregate wrapper (SUM(f_N) AS f_N); the tag /
		// dimension fields stay bare — mirrors the former GetSQLFieldName.
		const bool measure = (slot.m_role == ibColumnRole::Number || slot.m_role == ibColumnRole::Date);
		const ibString f = (!aggr.empty() && measure)
		                 ? aggr + ibT("(") + slot.m_name + ibT(") AS ") + slot.m_name
		                 : slot.m_name;
		out += out.empty() ? f : (ibT(",") + f);
	}
	return out;
}

ibString ColumnComparePredicate(const ibBackendQueryColumn& col, const ibString& cmp)
{
	ibString out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		// The discriminator and the reference TYPE id always match by equality; the value fields
		// (and the reference blob) use the caller's comparator — mirrors GetCompositeSQLFieldName.
		const bool eq = (slot.m_role == ibColumnRole::Discriminator || slot.m_role == ibColumnRole::ReferenceType);
		const ibString term = slot.m_name + ibT(" ") + (eq ? ibString(ibT("=")) : cmp) + ibT(" ?");
		out += out.empty() ? term : (ibT(" AND ") + term);
	}
	return out;
}
