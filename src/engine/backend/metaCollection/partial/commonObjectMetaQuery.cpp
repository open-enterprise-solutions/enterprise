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
#include "backend/query/dataQueryBuilder.h"                       // L3 write door (predefined seeding) + ibRawDBColumn
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
	return m_meta->FindObjectByFilter<ibValueMetaObjectAttributeBase>(name, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID, g_metaCommonAttributeColumnCLSID });
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
wxString ibRecordQueryable::GetQueryName()      const { return m_meta->GetName(); }
const ibMetaData* ibRecordQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
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
ibHierarchyType ibRecordQueryable::GetHierarchyType() const {
	// Same route as the column above: the metaobject answers from its own declaration, virtual dispatch
	// down its own inheritance, no cast. The ARRANGEMENT travels whole — each caller reads off it the
	// distinction it needs, instead of being handed one boolean per question.
	return m_meta->GetHierarchyType();
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
	return HasParentLink() ? GetDataParent() : nullptr;
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
wxString ibRegisterDataQueryable::GetQueryName()      const { return m_meta->GetName(); }
const ibMetaData* ibRegisterDataQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
const ibValueMetaObjectGenericData* ibRegisterDataQueryable::GetSourceMetaObject() const { return m_meta; }   // the metaobject behind the source (front reads its icon)
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

