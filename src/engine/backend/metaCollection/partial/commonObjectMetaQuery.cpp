////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metadata-side DATA glue - the queryable face and the row dump /
//	              restore, DELEGATED to the L3-3 mover (ibDataMover): this file only
//	              vends the table + columns + key shape. db_query (the local channel),
//	              no ses_query. The structure DECLARATION (ContributeTables ->
//	              ibSchemaSnapshot) moved out to commonObjectSchema.cpp.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject (materialisation)
#include "backend/query/dataQueryBuilder.h"                       // L3 write door (predefined seeding) + ibBackendColumnRawDB
#include "backend/objCtor.h"                                      // ibCtorMetaValueType (reference-target resolution)
#include "backend/system/value/valuePointInTime.h"                // g_valuePointInTimeCLSID — the moment column assembles one
#include "backend/metaData.h"                                     // ibMetaData::GetTypeCtor
#include "backend/databaseLayer/databaseQueryBuilder.h"           // ibDdlStatement / ibQueryStatement / ibQueryResult (L2)
#include "backend/query/columnLayout.h"                           // ColumnFieldNames (column field list via ibBackendQueryColumn)
#include "backend/query/schemaBuilder.h"                          // ibSchemaBuilder — the L3-2 schema door (DDL apply + barrier)
#include "backend/query/structureBatch.h"                         // ibStructureBatch — per-table DDL/seed batch the Process* fill
#include "backend/query/schemaSnapshot.h"                         // ibSchemaSnapshot / SnapshotOf — declarative structure (the differ's input)

wxString ibValueMetaObjectRecordDataRef::GetPhysicalTableName() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format(wxT("%s%i"),
		className, GetMetaID());
}

// --- value(<Kind>.<Name>.<Member>) resolution (L4-1 literal reference constant) ------------------------------
// A pure try-resolve: TRUE + the value in `out`, FALSE when the member is unknown (the query engine raises the
// exception, so the error carries the query source span — Max). The GENERIC metaobject has no constants; a
// reference record vends EmptyRef; the hierarchy level adds predefined items. No throw here.

bool ibValueMetaObjectGenericData::ResolveQueryConstant(const wxString& /*member*/, ibValue& /*out*/) const
{
	return false;
}

bool ibValueMetaObjectRecordDataRef::ResolveQueryConstant(const wxString& member, ibValue& out) const
{
	if (member.CmpNoCase(ibRefMember::EmptyRef) != 0)
		return false;
	out = ibValue(ibValueReferenceDataObject::Create(this));
	return true;
}

// ADDITIVE, like every other override in this family: the parent answers what it declares
// (EmptyRef, spelled ONCE where the reference itself lives) and the level below adds what it has.
// Re-spelling the base's own constant here would be a second place to change it.
bool ibValueMetaObjectRecordDataEnumRef::ResolveQueryConstant(const wxString& member, ibValue& out) const
{
	if (ibValueMetaObjectRecordDataRef::ResolveQueryConstant(member, out))
		return true;
	// The members ARE this object's constants — the same reference the manager hands a script
	// (ibValueManagerDataObjectEnumeration::GetPropVal), resolved by the name the author wrote.
	for (const ibValueMetaObjectEnum* object : GetEnumObjectArray()) {
		if (object == nullptr || object->GetName().CmpNoCase(member) != 0)
			continue;
		out = ibValue(ibValueReferenceDataObject::Create(this, object->GetGuid()));
		return true;
	}
	return false;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ResolveQueryConstant(const wxString& member, ibValue& out) const
{
	if (ibValueMetaObjectRecordDataRef::ResolveQueryConstant(member, out))
		return true;
	if (const wxObjectDataPtr<ibPredefinedValueObject> pv = FindPredefinedValue(member)) {
		out = ibValue(ibValueReferenceDataObject::Create(this, pv->GetPredefinedGuid()));
		return true;
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////

// (Table-data dump / restore is NOT per-object — the orchestrator drives the whole config's
//  ContributeTables snapshot through the L3-3 mover. See ibMetaDataConfigurationStorage::Dump/
//  RestoreDataFromBuffer in metadataConfiguration.cpp.)

wxString ibValueMetaObjectRegisterData::GetPhysicalTableName() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format(wxT("%s%i"),
		className, GetMetaID());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

// (RegisterData / RecordData dump & restore are GONE — the orchestrator moves every table's rows off
//  the config's ContributeTables snapshot through the L3-3 mover. A register simply declares its table
//  there; the mover reads "no uuid scaffold -> INSERT" off that structure.)

// --- vended queryables — the L3 navigation LIVES here, not on the metaobject ---
// The metaobject vends one of these (a stable member) via GetQueryable() and provides
// only primitives (FindObjectByFilter / GetPhysicalTableName / IsDataReference / guidName);
// the queryable owns the query-interface logic, reaching the concrete leaf through the
// metaobject's virtual primitives. (decouple §22.4e)

// catalog / document / charts / enums — identity = the row-key column (guidName);
// the reference attribute is the row's own key.
// (ibRecordQueryable's bodies are in commonObject.h, at the end — a template's definitions belong
//  where whoever instantiates it can see them.)
// The uniqueness key (UPSERT match + dot-walk self-reference + row identity) — the data-reference
// attribute (the pure-guid self-reference blob the row stores; type is the _RTRef column). The provider reads its
// Reference field for the join key and its fields for the match. No GetRowKeyColumn /
// IsReferenceAttribute / GetReferenceKeyColumn / GetIdentitySort — all of them derive from this one
// authority, and the last of them was retired for pretending to be a second one: it answered with a
// SORT whose tail happened to be the key, so a source that sorts by something else first (an
// enumeration, by Order) handed a number to everyone who wanted identity.
// (docs/query-language-arc.md §22.1)
// ⚠ THE ROW KEY IS NOT AN ALTERNATIVE ANSWER HERE, however well it fits a source that stores no reference
// of its own (an enumeration). This key is read by TWO tiers that want different things from it: the cursor
// expands it into PHYSICAL fields, where the row key is exactly right — and the composer writes its NAME into
// query TEXT, where the row key is `Row_RRRef`, a physical field the query language deliberately does not
// know (columnLayout.h § THE ROW KEY). Answering with it made the enum's ordering right and every
// text-rendered read wrong ("unknown attribute 'Row_RRRef'"), which is how the quick choice stopped opening.
// Whatever fixes an enum's ordering belongs at the tier that expands fields, not in what the key IS.
// ⭐⭐ THE MOMENT READS ITSELF — the one column here that does.
//
// It has no field, so there is no `_TYPE` tag to dispatch on and the default read cannot describe it.
// What it does have is the two columns it stands for: the DATE it happened at and the RECORD standing
// there. Each is read exactly as it is read anywhere else — through its own column, by its own rules —
// and the pair is handed to the value type, which is what knows how to be a moment.
//
// Nothing about moments is written in the codec, and nothing about columns is written in the value:
// the column asks for two values, the type makes one out of them.
ibRecorderQueryable::ibRecorderQueryable(const ibValueMetaObjectRecordDataRecorderRef* meta)
	: ibRecordQueryable<ibValueMetaObjectRecordDataRecorderRef>(meta), m_momentColumn(meta) {}

// The MOMENT answers first, then the attributes: a query naming it resolves to the column that knows
// how to build it. Without this the field tree offered a name the query could not resolve — a field
// that exists only on screen. (The tabular section answers `Ref` the same way.)
const ibBackendQueryColumn* ibRecorderQueryable::ResolveColumnByName(const wxString& name) const
{
	if (name.IsSameAs(m_momentColumn.GetName(), false))
		return &m_momentColumn;
	return ibRecordQueryable<ibValueMetaObjectRecordDataRecorderRef>::ResolveColumnByName(name);
}

// …and it is a field of this source like any other, so `SELECT *` carries it. Nothing stores it, so
// the projection writes no field for it; the READ constructs it out of the date and the reference,
// which the same select carries anyway.
std::vector<const ibBackendQueryColumn*> ibRecorderQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> cols =
		ibRecordQueryable<ibValueMetaObjectRecordDataRecorderRef>::GetColumns();
	cols.push_back(&m_momentColumn);
	return cols;
}

// THE MOMENT LIES IN TWO OTHER COLUMNS — the date first, then the reference. Each of them already
// describes itself (tag + value for the date; tag, metatype and identifier for the reference), so
// this is their layouts one after the other and nothing else. Sorting by the moment then IS sorting
// by the date and then by the identifier, through the machinery that sorts any reference by its own
// two fields — no new rule anywhere.
std::vector<ibColumnSlot> ibRecorderQueryable::ibBackendColumnPointInTime::DescribeLayout() const
{
	std::vector<ibColumnSlot> slots;
	for (const ibBackendQueryColumn* part : { m_owner->GetDocumentDate()->GetQueryColumn(), m_owner->GetDataReference()->GetQueryColumn() }) {
		if (part == nullptr) continue;
		for (const ibColumnSlot& slot : DescribeColumnLayout(part)) {
			// ⚠ WITHOUT THE PARTS' TYPE TAGS. A `_TYPE` field says WHICH of a column's admissible types
			// a row holds — a question the moment does not have: its date is always a date and its
			// reference always a reference, chosen when the pair was named and not per row. Carried
			// over, the two tags would also collide under one prefix, which is what Firebird said:
			// `-104 column POINTINTIME_TYPE was specified multiple times` (2026-08-23).
			if (slot.m_role == ibColumnRole::Discriminator)
				continue;
			slots.push_back(slot);
		}
	}
	return slots;
}

wxString ibRecorderQueryable::ibBackendColumnPointInTime::GetName() const { return m_owner->GetPointInTime()->GetName(); }
wxString ibRecorderQueryable::ibBackendColumnPointInTime::GetSynonym() const { return m_owner->GetPointInTime()->GetSynonym(); }
wxString ibRecorderQueryable::ibBackendColumnPointInTime::GetPhysicalName() const { return m_owner->GetPointInTime()->GetPhysicalName(); }
ibMetaID ibRecorderQueryable::ibBackendColumnPointInTime::GetColumnId() const { return m_owner->GetPointInTime()->GetColumnId(); }
ibTypeDescription& ibRecorderQueryable::ibBackendColumnPointInTime::GetTypeDesc() const { return m_owner->GetPointInTime()->GetTypeDesc(); }

bool ibRecorderQueryable::ibBackendColumnPointInTime::ReadValue(const wxString& fieldName,
	const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData) const
{
	const ibBackendQueryColumn* date = m_owner->GetDocumentDate()->GetQueryColumn();
	const ibBackendQueryColumn* ref = m_owner->GetDataReference()->GetQueryColumn();
	if (date == nullptr || ref == nullptr)
		return false;

	// ⚠ READ BY THE PARTS' OWN FIELD NAMES — `fld<date>_D`, `fld<ref>_RRRef` — because those are the
	// only fields that exist. The moment has none of its own: nothing projects it (its layout names
	// fields that belong to the date and the reference), and a declared query publishes those two
	// columns physically, under exactly the same names they carry in the table. So one spelling works
	// on both roads, and reading under this column's own name found nothing anywhere
	// (`Field 'fld1672_D' not found in the resultset`, 2026-08-23).
	//
	// The two reads are the ordinary leaves: a DATE field, and a REFERENCE taking its own pair off its
	// base name. No tag is consulted — the moment has none and needs none: what its halves are was
	// decided when the pair was named.
	ibValue vDate, vReference;
	ibColumnCodec::ReadField(date->GetPhysicalName() + ibFieldSuffix(ibColumnRole::Date), ibFieldTypes_Date,
	                         date, metaData, vDate, result, createData);
	ibColumnCodec::ReadField(ref->GetPhysicalName(), ibFieldTypes_Reference,
	                         ref, metaData, vReference, result, createData);

	retValue = new ibValuePointInTime(vDate.GetDateTime(), vReference);
	return true;
}

// ⭐ THE WRITING SIDE OF THE SAME COLUMN — and the thing that makes `WHERE Moment <= &Point` run.
//
// A comparison against a metadata column is decomposed field by field (DecomposeOrdered): the fields
// come from DescribeLayout above, and the CONSTANTS they are compared against are bound here. So the
// moment's halves must land in the same order the layout names them — the date, then the reference's
// pair — or the statement compares a date against a guid.
//
// ⚠ NOT THE CODEC'S SPREAD. It drives off a value TAG, and a moment has none: every slot would bind
// NULL, the filter would be `unknown` for every row, and a report asking for "everything up to this
// document" would come back empty without a word said. This is why the bind is a question for the
// COLUMN and not for the value's type.
//
// A BARE DATE is a legitimate right-hand side — a moment orders with one (ibValuePointInTime::
// CompareValueLS) — and means the instant ITSELF, before any record standing in it. That falls out of
// the encoding rather than being special-cased: an empty reference binds type 0, which sorts ahead of
// every real metatype, so `>= <date>` takes the whole day and `<= <date>` stops before it.
void ibRecorderQueryable::ibBackendColumnPointInTime::BindValue(ibQueryStatement& statement,
	const ibMetaData* /*metaData*/, const ibValue& value, int& position) const
{
	wxDateTime date;
	ibValue    reference;
	ibValuePointInTime* moment = nullptr;
	if (value.ConvertToValue(moment) && moment != nullptr) {
		date      = moment->m_date;
		reference = moment->m_reference;
	}
	else {
		date = value.GetDateTime();
	}

	// The reference pair, resolved once — the same two things the codec binds for a reference slot:
	// WHICH type (the clsid, as a number) and WHICH row (the key blob). Read off the value itself.
	ibClassID   refClsid = 0;
	const void* refBlob  = nullptr;
	ibValueReferenceDataObject* refData = nullptr;
	if (IsReference(reference.GetClassType()) && reference.ConvertToValue(refData) && refData != nullptr) {
		refClsid = reference.GetClassType();
		refBlob  = refData->GetReferenceData();
	}

	for (const ibColumnSlot& slot : DescribeLayout()) {
		switch (slot.m_role) {
		case ibColumnRole::Date:          statement.SetParamDate(position++, date); break;
		case ibColumnRole::ReferenceType: statement.SetParamNumber(position++, refClsid); break;
		case ibColumnRole::ReferenceId:
			if (refBlob != nullptr) statement.SetParamBlob(position++, refBlob, sizeof(ibReference));
			else                    statement.SetParamNull(position++);
			break;
		// The layout is the date's slots and the reference's, and those are the roles they have.
		// Anything else would be a field this column does not know it has, so it binds nothing
		// rather than guessing — and the position still advances, because the field is in the list.
		default:                          statement.SetParamNull(position++); break;
		}
	}
}


// The hierarchy metaobject's own parent column (a predefined attribute IS-A ibBackendQueryColumn) — defined
// out-of-line HERE where the attribute type is complete. The record queryable (above) forwards to it. (Folder
// column removed — folders are a creation-time sort/filter setting, not a structural column.)
const ibBackendQueryColumn* ibValueMetaObjectRecordDataHierarchyMutableRef::GetHierarchyColumn() const {
	// ⭐⭐ THE HIERARCHY IS THE PARENT. There is exactly ONE arrangement with no hierarchy — the one
	// with no parent at all (`None`), where the field is gone rather than merely unused, and where
	// "in hierarchy" can only ever answer with the value itself. Every other arrangement RECORDS a
	// parent, and a recorded parent IS a hierarchy: something to walk up and something to fold down.
	//
	// ⚠ It used to answer `IsHierarchical()` — the two arrangements the engine NAVIGATES — and that
	// made one accessor carry two different questions under one null: "is there a parent" and "does
	// the list drill". A chart of accounts declares Subordination (`chartOfAccountsMetadata.cpp`), so
	// it answered NO to both, and everything downstream that needed only the FIRST answer went quietly
	// without: `TOTALS BY <account> HIERARCHY` degraded to a flat grouping, and a filter asking in
	// hierarchy had to reach around this accessor into the metaobject to get an answer at all. The
	// enumeration says as much in its own words — *whoever wants the structure asks for it, a query,
	// a grouping* (`commonObjectEnum.h`) — and this is the accessor they ask.
	//
	// A FLAT list still answers null, which is what keeps a tree from being built over a column that
	// is not there.
	return HasParentLink() ? GetDataParent()->GetQueryColumn() : nullptr;
}
// ResolveReferenceTarget / ResolveReferenceTargets moved to ibDbTableProvider (query/dbTableProvider.cpp)
// — the ONE provider that owns metadata. The record queryable only vends GetMetaData(); the provider
// reads it off queryable->GetMetaData() and does clsid -> GetTypeCtor -> holder -> GetQueryable. The
// call sites now go through queryable->GetProvider().ResolveReferenceTarget(queryable, col). (docs §22)
// (Auto-join no longer needs dedicated self-reference / find-reference virtuals: the
// composer derives the join keys from the columns — a referencing column resolved by
// ResolveReferenceTarget, matched to the target's IsPrimaryKey column. The data-reference
// attribute reports IsPrimaryKey (it asks this record's IsDataReference).)
// (Row-key + attribute materialisation moved to the DB provider — it receives the column,
// static_casts it to the attribute, and calls GetValueAttribute; the row self-reference is
// built from the row guid + this source's metaID. The queryable names no attribute / L1.)

// registers — no single row-key; composite identity (recorder+line / period?+dims),
// carried as real attributes; the consumer assembles the row identity. No reference.
// ⭐ Answered with the attribute's QUERY FACE throughout this file: an attribute is not a query
// column, it holds one (docs/ownership-authority.md). The signatures are untouched.
const ibBackendQueryColumn* ibRegisterDataQueryable::ResolveColumnByName(const wxString& name) const {
	const ibValueMetaObjectAttributeBase* attribute = m_meta->FindAnyAttributeObjectByFilter(name);
	return attribute != nullptr ? attribute->GetQueryColumn() : nullptr;
}
std::vector<const ibBackendQueryColumn*> ibRegisterDataQueryable::GetColumns() const {
	// All generic attributes — the register's generic array ALREADY spans the
	// predefineds (recorder / line / period), the dimensions, the resources and
	// the plain attributes. Mirrors ibRecordQueryable. Drives the L5 composer's
	// default projection and SELECT * of a nested subquery.
	std::vector<const ibBackendQueryColumn*> cols;
	for (const ibValueMetaObjectAttributeBase* a : m_meta->GetGenericAttributeArrayObject())
		if (a != nullptr) cols.push_back(a->GetQueryColumn());
	return cols;
}
wxString ibRegisterDataQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
wxString ibRegisterDataQueryable::GetQueryName()      const { return m_meta->GetName(); }
const ibMetaData* ibRegisterDataQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
const ibValueMetaObjectGenericData* ibRegisterDataQueryable::GetSourceMetaObject() const { return m_meta; }   // the metaobject behind the source (front reads its icon)
// Uniqueness key (UPSERT match): recorder + line number + period for a recorder-based register
// (its dimensions are data); period + dimensions for an information register. The queryable is
// the authority — no per-column / per-attribute flag. (docs/query-language-arc.md §22.1)
std::vector<const ibBackendQueryColumn*> ibRegisterDataQueryable::GetPrimaryKeyColumns() const {
	std::vector<const ibBackendQueryColumn*> cols;
	// The key's parts, each by its query face — asked in one place so a missing one cannot slip in
	// as a null and be discovered by whoever hashes the key.
	auto add = [&cols](const ibValueMetaObjectAttributeBase* a) {
		if (a != nullptr) cols.push_back(a->GetQueryColumn());
	};
	if (m_meta->HasRecorder()) {
		add(m_meta->GetRegisterRecorder());
		add(m_meta->GetRegisterLineNumber());
		add(m_meta->GetRegisterPeriod());
		return cols;
	}
	if (m_meta->HasPeriod())
		add(m_meta->GetRegisterPeriod());
	for (auto* dim : m_meta->GetGenericDimensionArrayObject())
		add(dim);
	return cols;
}


