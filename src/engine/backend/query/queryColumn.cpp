////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : query column — what a column DOES: the fields it lays out
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE COLUMN'S OWN BEHAVIOUR, kept apart from the LAYOUT tier that asks for it.
//
// columnLayout.{h,cpp} answers "what physical fields does this column occupy" for every caller; the
// answer itself is the column's — a virtual whose default is here, and an override where a column is
// not stored the ordinary way (a raw field; a document's moment, which lives beside its own metatype).
//
// How a column is read out of a result and bound into a statement is the same arrangement, but those
// are a database result and a database statement: the defaults live beside the value codec, in the
// database provider (provider/database/columnCodec.cpp).

#include "backend/query/queryColumn.h"
#include "backend/query/columnLayout.h"                            // ibFieldSuffix

#include <atomic>                                                  // the synthetic-id counter below
#include "core/clsid.h"                                         // IsReference — a reference target by its clsid KIND
#include "backend/system/value/valueJob.h"                         // g_valueScheduleCLSID — a value stored whole
#include "backend/system/value/valueType.h"                        // g_valueTypeDescriptionCLSID — the other one; AdjustValue — a value of the type
#include "backend/valueInfo.h"                                     // reference_size_t — the width of the stored key

namespace {

// The canonical DDL type of one primitive slot, read off the column's type qualifiers (precision /
// scale / length / fixed / date fraction), expressed as L2 canonical types so the dialect dictionary
// closes the per-DBMS spelling — no BYTEA/BLOB, DATE/TIMESTAMP, CHAR/VARCHAR forks here.
ibColumnType PrimitiveType(ibValueTypes vt, const ibTypeDescription& td)
{
	switch (vt) {
	case ibValueTypes::TYPE_BOOLEAN:
		return ibTypeBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return ibTypeNumber(td.GetPrecision(), td.GetScale());
	case ibValueTypes::TYPE_DATE:
		switch (td.GetDateFraction()) {
		case ibDateFractions::ibDateFractions_Date: return ibTypeDate(ibDatePrec::Date);
		case ibDateFractions::ibDateFractions_Time: return ibTypeDate(ibDatePrec::Time);
		default:                                    return ibTypeDate(ibDatePrec::DateTime);
		}
	case ibValueTypes::TYPE_STRING:
		// a length of 0 is no limit (ibQualifierString) — a column of no limit, not the 255 a raw column's 0 is
		if (td.GetLength() == 0)
			return ibTypeText();
		return td.GetAllowedLength() == ibAllowedLength::ibAllowedLength_Fixed
		     ? ibTypeChar(td.GetLength())
		     : ibTypeString(td.GetLength());
	case ibValueTypes::TYPE_ENUM:
		return ibTypeInteger();   // enum stored as its int ordinal
	default:
		return ibTypeInteger();
	}
}

// Reached only under `col.IsRawColumn()` — the column has already SAID what it is, so the cast asks
// for the type it CARRIES rather than deciding identity (which is the column's own answer, never a
// cast's).
//
// ⚠ IT USED TO ASK RTTI AND, ON A MISS, RETURN VARCHAR(255) — on the reasoning that "a wrong-but-valid
// type is repairable where a throw is not". It is not repairable: this answer becomes a CREATE TABLE.
// A column silently created as text where a number belonged is a migration, discovered by arithmetic
// failing months later.
ibColumnType RawTypeOf(const ibBackendQueryColumn& col)
{
	const ibBackendColumnRawDB* const raw = col.AsRawColumn();
	ibASSERT_MSG(raw != nullptr,
		ibT("RawTypeOf asked of a column that does not carry its own physical type"));
	if (raw == nullptr)
		return ibTypeString(255);
	switch (raw->GetRawType()) {
	// ⭐ THE COLUMN'S OWN WIDTH WHERE IT DECLARED ONE. A raw column that says nothing gets the old
	// defaults; one that DOES say — an indexed digest, a totals figure carrying a resource's own
	// precision and scale — is created as it asked. Both were bugs while this ignored them:
	// VARCHAR(255) in UTF8 passes Firebird's index key ceiling by itself, and NUMERIC(18,0) under a
	// resource declared with kopecks drops the fraction on the way in.
	case ibBackendColumnRawDB::RawType::String:
		return ibTypeString(raw->GetRawLength() > 0 ? raw->GetRawLength() : 255);
	case ibBackendColumnRawDB::RawType::Number:
		return ibTypeNumber(raw->GetRawLength() > 0 ? raw->GetRawLength() : 18, raw->GetRawScale());
	case ibBackendColumnRawDB::RawType::Reference: return ibTypeBinary(reference_size_t);   // _RRRef fixed key
	case ibBackendColumnRawDB::RawType::Date:      return ibTypeDate();
	case ibBackendColumnRawDB::RawType::Boolean:   return ibTypeBoolean();
	// ⭐⭐ THE ROW KEY IS THE SAME SIXTEEN BYTES A REFERENCE KEY IS — one representation, so a row's
	// uuid can be COMPARED with a reference to that row (a section's link to its owner, a dot-walk, a
	// hand-written join). Two spellings of one identity could never meet; one spelling meets itself.
	case ibBackendColumnRawDB::RawType::Guid:      return ibTypeBinary(reference_size_t);
	case ibBackendColumnRawDB::RawType::Blob:      return ibTypeBlob();   // a register's rowData
	}
	return ibTypeString(255);
}

} // namespace

//***************************************************************************
//*                    the default — every ordinary column                  *
//***************************************************************************

// ⭐ THE DEFAULT LAYOUT — derived from (physical name, type), exactly as it always was. It lives here,
// with the other things a column DOES, and the layout tier asks for it through DescribeColumnLayout.
std::vector<ibColumnSlot> ibBackendQueryColumn::DescribeLayout() const
{
	std::vector<ibColumnSlot> slots;

	// A raw column is a single physical field with its own carried type — no spread.
	// ⭐⭐ ONE FIELD, AND THE KIND SAYS SO. A RAW column carries its own declared type in one physical
	// field; a COMPUTED one exists only in a result — an aggregate, an expression, a dot-walk leaf
	// minted under an alias — and is read back BY NAME from the cursor, which is one field too. The
	// kind's own definition says as much (queryColumn.h); the layout simply did not ask.
	//
	// 🛑 A COMPUTED COLUMN SPREAD LIKE A COMPOSITE, so everything downstream went looking for fields
	// nobody wrote. `34 AS YTFDS` grouped in a report produced *"Field 'YTFDS_TYPE' not found in the
	// resultset"* once per row read — the reader asking for a variant tag that a constant cannot have
	// (measured from the journal, 2026-08-24). The declaration published the name and the statement
	// wrote one column; only the layout believed there were four.
	if (IsRawColumn()) {
		ibColumnSlot slot;
		slot.m_name = GetPhysicalName();
		slot.m_role = ibColumnRole::Raw;
		slot.m_type = RawTypeOf(*this);
		slots.push_back(std::move(slot));
		return slots;
	}

	// …AND A COMPUTED ONE IS THE SAME SHAPE WITH NO TYPE. One field, read by name — but NOT
	// `RawTypeOf`: that answer becomes a `CREATE TABLE`, and it is asked of a column that declared a
	// physical type. Nothing declares a computed output, so it HAS none, and its own assert says so
	// (caught the first run after this branch was written, 2026-08-24 — the guard doing its job).
	//
	// The empty type is the honest answer here: this layout is read for the FIELD NAMES — a
	// projection, a published set, a sort key — never to create anything.
	if (GetColumnKind() == Kind::Computed) {
		ibColumnSlot slot;
		slot.m_name = GetPhysicalName();
		slot.m_role = ibColumnRole::Raw;
		slots.push_back(std::move(slot));
		return slots;
	}

	// What a VALUE here may be — not what is declared. The two differ only for a characteristic, whose
	// declaration names a class no value carries (backend_type.h).
	//
	// ⭐ …AND A COLUMN THAT DECLARES NO TYPE MAY HOLD ANY VALUE: it is laid out as the WIDEST COMPOSITE — every primitive
	// in a field of its own and of no limit, a reference as its pair, and in _V only what has no field of its own (an
	// object, a member of an enumeration, a schedule). Kept in fields, its numbers still sum and its keys still order
	// (Max, 2026-11-18); kept whole as one node, they did neither.
	static const ibTypeDescription s_anyPrimitive(
		{ g_valueBooleanCLSID, g_valueNumberCLSID, g_valueDateCLSID, g_valueStringCLSID },
		ibQualifierNumber(0, 0), ibQualifierDate(ibDateFractions::ibDateFractions_DateTime), ibQualifierString(0));   // 0 — no limit
	const bool any = GetTypeValueDesc().GetClsidCount() == 0;
	const ibTypeDescription& td = any ? s_anyPrimitive : GetTypeValueDesc();
	const ibString base = GetPhysicalName();

	// A slot named off its role through the one suffix table.
	auto makeSlot = [&](ibColumnRole role, ibColumnType type, bool notNull) {
		ibColumnSlot slot;
		slot.m_name    = base + ibFieldSuffix(role);
		slot.m_role    = role;
		slot.m_type    = std::move(type);
		slot.m_notNull = notNull;
		return slot;
	};

	// ⭐ THE FIELDS NO WRITE LEAVES NULL ARE NOT NULL — the tag, every primitive (an absent one binds its placeholder,
	// BindAbsentPrimitive) and the class; each is created holding what the column's write puts there for a fresh row
	// (structureBatch.cpp). The key is not: a composite binds none for what is no reference, and a column
	// widened from one reference to more keeps its field as it was created.

	// _TYPE — the variant discriminator. Always present, NOT NULL.
	slots.push_back(makeSlot(ibColumnRole::Discriminator, ibTypeInteger(), true));

	// Primitive value slots — in the FIXED order B, N, D, S, E (load-bearing: matches the keyset
	// anchor + the write/read bind order).
	auto pushPrim = [&](ibValueTypes vt, ibColumnRole role) {
		if (td.ContainType(vt))
			slots.push_back(makeSlot(role, PrimitiveType(vt, td), true));
	};
	pushPrim(ibValueTypes::TYPE_BOOLEAN, ibColumnRole::Boolean);
	pushPrim(ibValueTypes::TYPE_NUMBER,  ibColumnRole::Number);
	pushPrim(ibValueTypes::TYPE_DATE,    ibColumnRole::Date);
	pushPrim(ibValueTypes::TYPE_STRING,  ibColumnRole::String);
	pushPrim(ibValueTypes::TYPE_ENUM,    ibColumnRole::Enum);

	// _SCH — a schedule, serialised whole. A BLOB rather than fourteen columns: what people actually
	// filter on is WHEN THIS RUNS NEXT, and that is a date column of its own on the job row
	// (docs/private/scheduled-jobs.md § 5b).
	if (td.ContainType(g_valueScheduleCLSID))
		slots.push_back(makeSlot(ibColumnRole::Schedule, ibTypeBlob(), false));

	// _TD — a type description, serialised whole. This is what a characteristic's own Type is: a
	// FILTER carrying admissible types and their qualifiers. Stored as one blob for the same reason
	// the schedule is — nothing predicates on it in SQL.
	if (td.ContainType(g_valueTypeDescriptionCLSID))
		slots.push_back(makeSlot(ibColumnRole::TypeDescription, ibTypeBlob(), false));

	// Reference pair — _RTRef (target clsid, BIGINT) + _RRRef (pure guid blob, fixed-width BINARY so
	// it is indexable for the dot-walk = join). Present when ANY clsid in the type is a reference, or the column declares
	// no type at all.
	bool hasReference = any;
	for (const auto& clsid : td.GetClsidList())
		if (IsReference(clsid)) { hasReference = true; break; }
	if (hasReference) {
		slots.push_back(makeSlot(ibColumnRole::ReferenceType, ibTypeBigInt(),                 true));
		slots.push_back(makeSlot(ibColumnRole::ReferenceId,   ibTypeBinary(reference_size_t), false));
	}

	// _V — what a column of no type holds and none of the fields above would, as its node's bytes (the codec writes
	// them, WriteValue). Every declared attribute has a type, so no table of an application has one.
	if (any)
		slots.push_back(makeSlot(ibColumnRole::Value, ibTypeBlob(), false));

	return slots;
}

// ⭐ THE TYPE FACTORY'S PAIR (ibBackendTypeApplicationFactory) — of the column's type, made by its application. A fresh
// value of a column with nothing behind it is its type's empty.
ibValue ibBackendQueryColumn::CreateValue() const
{
	return AdjustValue();
}

ibValue ibBackendQueryColumn::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc(), GetMetaData());
}

ibValue ibBackendQueryColumn::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc(), varValue, GetMetaData());
}
