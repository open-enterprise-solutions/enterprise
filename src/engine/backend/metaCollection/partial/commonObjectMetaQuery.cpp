////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metadata-side schema + data glue. The metaobject DECLARES its
//	              structure (ContributeTables -> ibSchemaSnapshot, applied by the
//	              L3-2 schema door) and DELEGATES its row dump / restore to the
//	              L3-3 mover (ibDataMover): it only vends the table + columns +
//	              key shape. db_query (the local channel), no ses_query.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject (materialisation)
#include "backend/query/dataQueryBuilder.h"                       // L3 write door (predefined seeding) + ibRawDBColumn
#include "backend/databaseLayer/databaseResultSet.h"              // GetResultString
#include "backend/objCtor.h"                                      // ibCtorMetaValueType (reference-target resolution)
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
	if (member.CmpNoCase(wxT("EmptyRef")) != 0)
		return false;
	out = ibValue(ibValueReferenceDataObject::Create(this));
	return true;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ResolveQueryConstant(const wxString& member, ibValue& out) const
{
	if (member.CmpNoCase(wxT("EmptyRef")) == 0) {
		out = ibValue(ibValueReferenceDataObject::Create(this));
		return true;
	}
	if (const wxObjectDataPtr<ibPredefinedValueObject> pv = FindPredefinedValue(member)) {
		out = ibValue(ibValueReferenceDataObject::Create(this, pv->GetPredefinedGuid()));
		return true;
	}
	return false;
}

// (SnapshotOf removed: building a snapshot is just `common->ContributeTables(snap)` — the metadata side
//  does it directly where it drives the builder, keeping the builder itself config-agnostic.)

namespace {
// One secondary DB index per indexed attribute. Index -> the attribute alone; IndexWithAdditionalOrder ->
// the attribute plus the row reference, so ordered list browsing reads its order straight off the index.
// Non-unique — an attribute value repeats. Named <table>_<attrId>_IX (metaID key: stable and short). A
// register has no single row reference, so its caller passes orderRef = nullptr and the ordered variant
// degrades to a plain index there.
void ContributeAttributeIndexes(ibSchemaTable& t,
	const std::vector<ibValueMetaObjectAttributeBase*>& attributes,
	const ibBackendQueryColumn* orderRef = nullptr)
{
	for (const auto attr : attributes) {
		const ibIndexingMode mode = attr->GetIndexingMode();
		if (mode == ibIndexingMode::ibIndexingMode_DontIndex)
			continue;
		std::vector<const ibBackendQueryColumn*> cols = { attr };
		if (mode == ibIndexingMode::ibIndexingMode_IndexWithAdditionalOrder && orderRef != nullptr && orderRef != attr)
			cols.push_back(orderRef);
		t.Index(wxString::Format(wxT("%s_%i_IX"), t.m_name, (int)attr->GetColumnId()), std::move(cols));
	}
}
} // namespace

void ibValueMetaObjectRecordDataMutableRef::ContributeTables(ibSchemaSnapshot& out) const
{
	// --- the record's main table ---
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());
	const ibBackendQueryColumn* uuid = t.Scaffold(ibRawDBColumn::Guid(wxT("uuid")));   // uuid row-key

	for (const auto object : GetPredefinedAttributeArrayObject())
		t.Add(object);
	for (const auto object : GetAttributeArrayObject())
		t.Add(object);

	t.Index(t.m_name + wxT("_INDEX"), { uuid }, true);                        // uuid uniqueness (replaced the old PRIMARY KEY)
	if (const ibValueMetaObjectAttributeBase* refAttr = GetDataReference())
		t.Index(t.m_name + wxT("_REF_UQ"), { refAttr }, true);               // _REF_UQ — the data-reference unique key

	// Per-attribute secondary indexes (plain + predefined: Code / Description / Parent / ...) —
	// the row reference is the additional-order column. Each carries its own Indexing flag.
	ContributeAttributeIndexes(t, GetAttributeArrayObject(), GetDataReference());
	ContributeAttributeIndexes(t, GetPredefinedAttributeArrayObject(), GetDataReference());

	// --- tabular sections — each its own table ---
	for (const auto tab : GetTableArrayObject()) {
		ibSchemaTable& tt = out.CreateSchemaTable(tab->GetQueryable());
		const ibBackendQueryColumn* tabUuid = tt.Scaffold(ibRawDBColumn::Guid(wxT("uuid")));
		for (const auto object : tab->GetGenericAttributeArrayObject())
			tt.Add(object);
		tt.Index(tt.m_name + wxT("_INDEX"), { tabUuid });                     // tabular uuid index — NOT unique (repeats per owner)
		// Per-attribute secondary indexes on the section's own columns — no single row reference
		// (browsing is per-owner by line), so the ordered variant degrades to a plain index here.
		ContributeAttributeIndexes(tt, tab->GetGenericAttributeArrayObject());
	}
}

void ibValueMetaObjectRecordDataEnumRef::ContributeTables(ibSchemaSnapshot& out) const
{
	// The enum table is just the uuid key — the enum VALUES are its SEED rows (data), not columns.
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());
	const ibBackendQueryColumn* uuid = t.Scaffold(ibRawDBColumn::Guid(wxT("uuid")));
	t.Index(t.m_name + wxT("_INDEX"), { uuid }, true);

	// SEED rows: one per enum value, uuid only (no cells) — the builder diffs by uuid (new -> insert,
	// gone -> delete); there is no per-value content to compare.
	for (const auto object : GetEnumObjectArray())
		t.AddRow(object->GetGuid().str(), object->GetFullName());
}

void ibValueMetaObjectRegisterData::ContributeTables(ibSchemaSnapshot& out) const
{
	// No rowData blob scaffold: it was the last trace of single-blob register storage. Nothing
	// ever wrote or read it (the column was DDL-only), yet the accumulation register folds
	// t.m_scaffold into its SelfSource projection — so every totals fold and every derived-table
	// regeneration was selecting an always-NULL column on every movement row. Dropped 2026-08-02.
	// Existing databases keep the physical column: scaffold columns carry no model id, so the
	// schema differ does not track them and will not issue a DROP.
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	for (const auto object : GetPredefinedAttributeArrayObject())
		t.Add(object);
	for (const auto object : GetDimensionArrayObject())
		t.Add(object);
	for (const auto object : GetResourceArrayObject())
		t.Add(object);
	for (const auto object : GetAttributeArrayObject())
		t.Add(object);

	// The key index: recorder + line for a subordinate register, else the dimension columns
	// (period + dimensions when periodic — GetGenericDimensionArrayObject prepends the period).
	// UNIQUE — these columns ARE the register's key: at most one record per key, same as objects
	// key on a unique uuid. It is the DB-level guarantee against duplicate rows; the app-level
	// ExistData probe alone (a fragile string compare) let dimension-key duplicates slip in.
	std::vector<const ibBackendQueryColumn*> idxCols;
	if (HasRecorder()) {
		idxCols.push_back(GetRegisterRecorder());
		idxCols.push_back(GetRegisterLineNumber());
	}
	else {
		for (const auto object : GetGenericDimensionArrayObject())
			idxCols.push_back(object);
	}
	if (!idxCols.empty())
		t.Index(t.m_name + wxT("_INDEX"), idxCols, true);

	// Per-field secondary indexes. Dimensions, resources, attributes and predefined all carry the
	// Indexing flag (each is-a ibValueMetaObjectAttribute), so GetGenericAttributeArrayObject covers
	// them all. A register has no single row reference, so the ordered variant degrades to a plain
	// index here (the dimensions already ride the composite key index above).
	ContributeAttributeIndexes(t, GetGenericAttributeArrayObject());
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::ContributeTables(ibSchemaSnapshot& out) const
{
	// STRUCTURE: the base builds the main record table (+ tabular sections) and Adds it to the snapshot.
	ibValueMetaObjectRecordDataMutableRef::ContributeTables(out);

	// SEED: one row per predefined value, cells bound to the SAME columns the structure declared (the data
	// reference / name / code / description / isFolder / parent / DeletionMark). The builder diffs by uuid +
	// cells and writes generically. DeletionMark = false here, so a re-added value reactivates on the next
	// apply. A reference cell MUST be a reference-typed value — the column codec extracts the reference
	// binary FROM a reference value (a plain guid string would write NULL); wxNullGuid = a null reference.
	//
	// The DATA-REFERENCE cell is the row's OWN reference (guid = its predefined guid = its uuid, this
	// metaID): it (a) makes the predefined value referenceable by its key, and (b) gives each row a UNIQUE
	// _RRRef so the data-reference unique index (_REF_UQ) does not see N rows colliding on the NULL default.
	ibSchemaTable& t = out.Shared(GetMetaID(), GetPhysicalTableName());   // the main table the base just created
	for (const auto& object : m_predefinedObjectVector) {
		const wxObjectDataPtr<ibPredefinedValueObject>& parent = object->GetPredefinedParent();

		t.AddRow(object->GetPredefinedGuid().str(), object->GetPredefinedName())
			.Set(GetDataReference(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::Create(this, object->GetPredefinedGuid())))
			.Set(m_propertyAttributePredefined->GetMetaObject(), ibValue(object->GetPredefinedName()))
			.Set(m_propertyAttributeCode->GetMetaObject(), ibValue(object->GetPredefinedCode()))
			.Set(m_propertyAttributeDescription->GetMetaObject(), ibValue(object->GetPredefinedDescription()))
			.Set(m_propertyAttributeIsFolder->GetMetaObject(), ibValue(object->IsPredefinedFolder()))
			.Set(m_propertyAttributeDeletionMark->GetMetaObject(), ibValue(false))
			.Set(m_propertyAttributeParent->GetMetaObject(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::Create(this, parent != nullptr ? parent->GetPredefinedGuid() : wxNullGuid)));
	}
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
const ibBackendQueryColumn* ibRecordQueryable::ResolveColumnByName(const wxString& name) const {
	// The attribute by name AS a column (an attribute IS-A ibBackendQueryColumn). The L3
	// surface returns a column; the DB provider static_casts it back to the attribute when
	// it needs the field machinery — the queryable itself names no attribute on its contract.
	return m_meta->FindObjectByFilter<ibValueMetaObjectAttributeBase>(name, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
}
std::vector<const ibBackendQueryColumn*> ibRecordQueryable::GetColumns() const {
	// All generic attributes (predefined + plain) — each IS-A column. Drives SELECT * of a
	// nested subquery over this source.
	std::vector<const ibBackendQueryColumn*> cols;
	for (const ibValueMetaObjectAttributeBase* a : m_meta->GetGenericAttributeArrayObject())
		cols.push_back(a);
	return cols;
}
wxString ibRecordQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
ibGuid ibRecordQueryable::GetQueryTableGuid() const { return  m_meta->GetGuid(); }
wxString ibRecordQueryable::GetQueryName()      const { return m_meta->GetName(); }
ibMetaID ibRecordQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
const ibMetaData* ibRecordQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
std::vector<ibQuerySortItem> ibRecordQueryable::GetIdentitySort() const {
	// The row identity is a REAL column now — the uuid, exactly like a register's identity is
	// real recorder/line/dimension columns (no null sentinel, no GetRowKeyColumn special case;
	// Pass 1 of BuildSortKeys consumes it uniformly). uuid stays as this rudiment key-column
	// until it is cleaned up; the function-local static gives a stable address for the sort item.
	static const ibRawDBColumn s_uuidKey(guidName, ibRawDBColumn::RawType::String);
	return { ibQuerySortItem{ &s_uuidKey, true } };
}
// The uniqueness key (UPSERT match + dot-walk self-reference + row identity) — the data-reference
// attribute (the pure-guid self-reference blob the row stores; type is the _RTRef column). The provider reads its
// Reference field for the join key and its fields for the match; uuid stays as a second link key
// (GetIdentitySort) until cleaned. No GetRowKeyColumn / IsReferenceAttribute / GetReferenceKeyColumn:
// all three are derived from this one authority. (docs/query-language-arc.md §22.1)
std::vector<const ibBackendQueryColumn*> ibRecordQueryable::GetPrimaryKeyColumns() const {
	const ibValueMetaObjectAttributeBase* refAttr = m_meta->GetDataReference();
	if (refAttr == nullptr)
		return {};
	return { refAttr };
}
const ibBackendQueryColumn* ibRecordQueryable::GetHierarchyColumn() const {
	// The metaobject knows if it is hierarchical — its CLASS is the signal (a catalog / chart IS one). It returns
	// its parent column through a virtual, null on a flat ref. No cast — virtual dispatch down its OWN inheritance.
	return m_meta->GetHierarchyColumn();
}

// The metaobject behind the source — the front reads its icon / presentation off it (via the dynamic list's
// GetSourceMetaObject → GetSourceQueryable → here). Out-of-line because the metaobject type is only complete in
// this TU (the upcast RecordDataRef → GenericData needs the full class).
const ibValueMetaObjectGenericData* ibRecordQueryable::GetSourceMetaObject() const {
	return m_meta;
}

// The hierarchy metaobject's own parent column (a predefined attribute IS-A ibBackendQueryColumn) — defined
// out-of-line HERE where the attribute type is complete. The record queryable (above) forwards to it. (Folder
// column removed — folders are a creation-time sort/filter setting, not a structural column.)
const ibBackendQueryColumn* ibValueMetaObjectRecordDataHierarchyMutableRef::GetHierarchyColumn() const {
	return GetDataParent();
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
const ibBackendQueryColumn* ibRegisterDataQueryable::ResolveColumnByName(const wxString& name) const { return m_meta->FindAnyAttributeObjectByFilter(name); }
std::vector<const ibBackendQueryColumn*> ibRegisterDataQueryable::GetColumns() const {
	// All generic attributes — the register's generic array ALREADY spans the
	// predefineds (recorder / line / period), the dimensions, the resources and
	// the plain attributes; each IS-A column. Mirrors ibRecordQueryable. Drives
	// the L5 composer's default projection and SELECT * of a nested subquery.
	std::vector<const ibBackendQueryColumn*> cols;
	for (const ibValueMetaObjectAttributeBase* a : m_meta->GetGenericAttributeArrayObject())
		cols.push_back(a);
	return cols;
}
wxString ibRegisterDataQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
ibGuid ibRegisterDataQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
wxString ibRegisterDataQueryable::GetQueryName()      const { return m_meta->GetName(); }
ibMetaID ibRegisterDataQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
const ibMetaData* ibRegisterDataQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
const ibValueMetaObjectGenericData* ibRegisterDataQueryable::GetSourceMetaObject() const { return m_meta; }   // the metaobject behind the source (front reads its icon)
std::vector<ibQuerySortItem> ibRegisterDataQueryable::GetIdentitySort() const {
	std::vector<ibQuerySortItem> ret;
	if (m_meta->HasRecorder()) {
		ret.push_back({ m_meta->GetRegisterRecorder(), true });
		ret.push_back({ m_meta->GetRegisterLineNumber(), true });
		return ret;
	}
	if (m_meta->HasPeriod())
		ret.push_back({ m_meta->GetRegisterPeriod(), true });
	for (auto* dim : m_meta->GetGenericDimensionArrayObject())
		ret.push_back({ dim, true });
	return ret;
}
// Uniqueness key (UPSERT match): recorder + line number + period for a recorder-based register
// (its dimensions are data); period + dimensions for an information register. The queryable is
// the authority — no per-column / per-attribute flag. (docs/query-language-arc.md §22.1)
std::vector<const ibBackendQueryColumn*> ibRegisterDataQueryable::GetPrimaryKeyColumns() const {
	std::vector<const ibBackendQueryColumn*> cols;
	if (m_meta->HasRecorder()) {
		cols.push_back(m_meta->GetRegisterRecorder());
		cols.push_back(m_meta->GetRegisterLineNumber());
		cols.push_back(m_meta->GetRegisterPeriod());
		return cols;
	}
	if (m_meta->HasPeriod())
		cols.push_back(m_meta->GetRegisterPeriod());
	for (auto* dim : m_meta->GetGenericDimensionArrayObject())
		cols.push_back(dim);
	return cols;
}

