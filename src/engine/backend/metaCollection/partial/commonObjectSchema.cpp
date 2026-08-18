////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the record / register families - the STRUCTURE they declare
//
//	ContributeTables and nothing else: what each family becomes in the database - its table, its
//	columns, its indexes, its seed rows, and the rules those tables carry into the restructuring.
//	Split out of commonObjectMetaQuery.cpp, which keeps the QUERY surface (the vended queryables,
//	the value materialisation, the constant resolve). "What does this look like in the database"
//	is one question and now has one file per family, the way accumulationRegisterMetadataSchema.cpp
//	already did for the totals-bearing register.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject - the seed's reference cells
#include "backend/query/columnLayout.h"                           // ibOwnerRefColumn / ibFieldSuffix - the scaffold + field names
#include "backend/query/schemaSnapshot.h"                         // ibSchemaSnapshot / ibSchemaTable - what a declaration is
#include "backend/databaseLayer/databaseQueryBuilder.h"           // the L2 door - the rules count rows through it
#include "backend/restructureInfo.h"                              // ibRestructureInfo - where a refused rule states its reason

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
	// ⭐⭐ NO ROW-KEY SCAFFOLD: A REFERENCE OBJECT IS IDENTIFIED BY ITS REFERENCE. The scaffold held
	// the same sixteen bytes the reference column holds, so the row carried its identity twice and
	// every tier had to know which of the two it was looking at. It stays where a row has no reference
	// of its own - a tabular section's line, identified by its owner and its number, and cheaper for
	// it: those tables are large, their rows repeat one owner, and no type varies down the column.
	// A reference object is the opposite case, so it keeps the whole reference and nothing beside it.
	//
	// Existing databases keep the physical column and lose nothing: a scaffold carries no model id,
	// so the differ never tracked it and will not drop it.
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	// THE GENERIC LIST, not predefined + own: it is the one that also carries the common
	// attributes this object was checked into. They are columns like any other — but they
	// are NOT in GetAttributeArrayObject, because that list answers "what did a person add
	// to this object", and the designer tree shows it. A common attribute is not shown
	// there, for the same reason Code and Description are not: it belongs to the
	// declaration, not to this object.
	for (const auto object : GetGenericAttributeArrayObject())
		t.Add(object);

	if (const ibValueMetaObjectAttributeBase* refAttr = GetDataReference())
		t.Index(t.m_name + wxT("_REF_UQ"), { refAttr }, true);               // _REF_UQ — the identity, and the only one

	// Per-attribute secondary indexes (plain + predefined: Code / Description / Parent / ...) —
	// the row reference is the additional-order column. Each carries its own Indexing flag.
	// Same list as the columns above — each attribute carries its own Indexing flag, and a
	// common attribute may be indexed exactly like the object's own.
	ContributeAttributeIndexes(t, GetGenericAttributeArrayObject(), GetDataReference());

	// --- tabular sections — each its own table ---
	for (const auto tab : GetTableArrayObject()) {
		ibSchemaTable& tt = out.CreateSchemaTable(tab->GetQueryable());
		const ibBackendQueryColumn* tabUuid = tt.Scaffold(ibOwnerRefColumn());
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
	// The enum table is its attribute columns; the enum VALUES are its SEED rows (data). No row-key
	// scaffold - see the record variant above: the reference IS the identity.
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	// ⭐⭐ ITS ATTRIBUTES ARE ITS COLUMNS — the same walk the record family makes, and the whole of what an
	// enumeration was missing. It declares the SAME predefined attributes every reference object does (the
	// data reference, and its Order), and it kept none of them: the table was the row key and nothing else.
	// So the reference existed in the metadata and nowhere in the database, and every tier that names
	// physical fields named fields nobody had written — the cursor's identity tail asked for
	// `fld<id>_RRRef`, Firebird answered "Column unknown", and no enum list or choice form could open.
	//
	// Nothing here is special-cased for enums: it is what a catalog does, applied to an object whose rows
	// happen to be declared rather than entered. Storing them costs nothing worth counting either — an
	// enumeration holds ten or fifteen values, not a million.
	for (const auto object : GetGenericAttributeArrayObject())
		t.Add(object);

	// The data-reference unique key, as on the record side: each row's own reference, unique per row.
	const ibValueMetaObjectAttributeBase* reference = GetDataReference();
	if (reference != nullptr)
		t.Index(t.m_name + wxT("_REF_UQ"), { reference }, true);

	const ibValueMetaObjectAttributeBase* order = GetDataOrder();

	// SEED rows: one per enum value — the builder diffs by uuid (new -> insert, gone -> delete) and
	// writes the cells, so the position travels with the row and needs no second mechanism to maintain.
	int position = 0;
	for (const auto object : GetEnumObjectArray()) {
		ibSchemaSeedRow& row = t.AddRow(object->GetGuid(), object->GetFullName());
		// The row's own reference, written like a predefined item's — the value IS the row, so the cell
		// carries the same guid the key does, and every read gets a reference without rebuilding one.
		// RAW, never Create: a seed cell needs the reference's IDENTITY BYTES, and Create materialises
		// the object - it goes to read the row it points at, from a table this very declaration is
		// about to create. Five SELECTs against a table that does not exist yet, five swallowed
		// exceptions, on every apply. (The record family writes its own reference the same way.)
		if (reference != nullptr)
			row.Set(reference, ibValuePtr<ibValueReferenceDataObject>(
				ibValueReferenceDataObject::CreateRaw(this, object->GetGuid())));
		if (order != nullptr)
			row.Set(order, ibValue(position));
		position++;
	}
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

	// ⭐⭐ THE PERIOD IS A READ PATH, NOT A KEY — and a subordinate register had no index on it.
	//
	// Its key is (Recorder, LineNumber), which answers "the lines of this document" and nothing else.
	// Every reading that matters asks the opposite question: the movements of an INTERVAL. Those are
	// the sub-grain tail of a totals read ("the balance at noon" = the stored total at midnight plus
	// today's movements), the boundary that names a document, and any direct read of the register
	// over dates. Without this index each of them is a full scan of the movements — bounded by the
	// register's whole history rather than by the day asked for, which is the difference between a
	// range read and a table that grows until reports stop.
	//
	// (Period, Recorder) in that order, because it is the order a moment is COMPARED in
	// (ibValuePointInTime::CompareValueLS): the same index serves "up to this instant" and "up to
	// this document within the instant", and the second needs no re-sort.
	//
	// ⚠ NOT the line number. It already rides the key index above, where it is asked for; on its own
	// it is a small integer repeated across every document — an index the planner would never choose
	// and every INSERT would pay for. A fold over the tail sums lines, and a sum has no order.
	if (HasRecorder() && GetRegisterPeriod() != nullptr && GetRegisterRecorder() != nullptr)
		t.Index(t.m_name + wxT("_PIX"), { GetRegisterPeriod(), GetRegisterRecorder() });

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

	// RETIRING A COLUMN IS DELETING WHAT IT HOLDS. Changing the hierarchy kind is a declaration and reads
	// like a setting, but two of its answers take columns away: "no subordination" retires Parent, and
	// anything but folders+items retires IsFolder (ApplyHierarchyType disables them, so the walk above
	// stops declaring them and the differ drops them). Rows that filled those columns lose their place in
	// the tree with no way back and no word said.
	//
	// Same shape as the chart of accounts' ceiling: the rule travels with the declaration, the differ asks
	// it before touching this table, and a refusal states its reason in the ledger — which greys Apply.
	{
		const wxString tableName = GetPhysicalTableName();
		const wxString objectName = GetName();
		const ibValueMetaObjectAttributeBase* parentAttr = GetDataParent();
		const ibValueMetaObjectAttributeBase* folderAttr = GetDataIsFolder();
		const bool losesParent  = !HasParentLink() && parentAttr != nullptr;
		const bool losesFolders = !HasFolders()    && folderAttr != nullptr;

		if (losesParent || losesFolders) {
			// THE PARENT IS FILLED WHEN ITS TARGET TYPE IS SET, not when its key is non-null: an empty
			// reference is stored as an ALL-ZERO guid (valueInfo.h), so testing the _RRRef blob for NULL
			// would count every row alive and refuse every time. The _RTRef id is 0 exactly when nothing
			// is referenced.
			const wxString parentField = losesParent  ? parentAttr->GetPhysicalName() + ibFieldSuffix(ibColumnRole::ReferenceType) : wxString();
			const wxString folderField = losesFolders ? folderAttr->GetPhysicalName() + ibFieldSuffix(ibColumnRole::Boolean)     : wxString();

			t.m_beforeChange = [tableName, objectName, parentField, folderField](ibRestructureInfo* report) -> bool {
				// Counted by the database, compared here — see the note on the chart of accounts' rule for
				// why the comparison does not ride in SQL.
				//
				// ⭐⭐ AND A FAILED COUNT IS NOT A COUNT OF ZERO. This used to answer 0 to any exception,
				// which reads as "nobody is in the way" — the strongest possible permission, granted
				// precisely when the rule could not establish anything. What it guards is not cosmetic:
				// zero here lets a hierarchy be dropped out from under rows that have a parent.
				//
				// The one legitimate reason to find nothing is a table that does not exist yet, and that
				// is asked once, in front. Anything else stops the apply.
				ibDatabaseQueryBuilder probe;
				if (!probe.TableExists(tableName))
					return true;   // nothing was ever applied here; no row can be stranded

				auto rowsWith = [&tableName](const wxString& field, const ibQueryExprPtr& filled) -> int {
					ibDatabaseQueryBuilder q;
					ibQueryIR ir;
					ir.m_root = ibAggregate(ibFilter(ibScan(tableName), filled),
						{ { ibFunc(wxT("COUNT"), { ibCol(field) }), wxT("rowCount") } }, {});
					ibQueryResult rs = q.ExecuteIR(ir);
					return rs.Next() ? rs.GetResultInt(wxT("rowCount")) : 0;
				};

				bool allowed = true;
				if (!parentField.IsEmpty()) {
					const int nested = rowsWith(parentField,
						ibBinOp(ibQueryBinOp::Ne, ibCol(parentField), ibConst(ibValue(0))));
					if (nested > 0) {
						allowed = false;
						if (report != nullptr)
							report->AppendError(wxString::Format(
								_("%s: %i row(s) have a parent - clear it before dropping the hierarchy"),
								objectName, nested));
					}
				}
				if (!folderField.IsEmpty()) {
					const int folders = rowsWith(folderField,
						ibBinOp(ibQueryBinOp::Eq, ibCol(folderField), ibConst(ibValue(true))));
					if (folders > 0) {
						allowed = false;
						if (report != nullptr)
							report->AppendError(wxString::Format(
								_("%s: %i folder(s) exist - delete them before switching to a hierarchy without folders"),
								objectName, folders));
					}
				}
				return allowed;
			};
		}
	}

	// RAW, never Create - the same rule the enum's seed above states, and this is the site it was
	// written for: a seed cell needs the reference's IDENTITY BYTES, while Create MATERIALISES the
	// object, going off to read the row it points at through the L3 door. That read selects every
	// attribute this metaobject declares - including the one this very apply is about to ADD - so it
	// asks the base for a column that does not exist yet and fails once PER PREDEFINED ITEM, at
	// DECLARATION time, before a single statement has been emitted. ("Field 'fldNNNN_TYPE' not found
	// in the resultset", twelve times over, in front of the real failure.)
	//
	// It is also the schema-authority rule (docs/schema-authority.md): what DDL to emit is decided by
	// the two configurations, never by asking the database what it currently holds.
	for (const auto& object : m_predefinedObjectVector) {
		const wxObjectDataPtr<ibPredefinedValueObject>& parent = object->GetPredefinedParent();

		t.AddRow(object->GetPredefinedGuid(), object->GetPredefinedName())
			.Set(GetDataReference(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::CreateRaw(this, object->GetPredefinedGuid())))
			.Set(m_propertyAttributePredefined->GetMetaObject(), ibValue(object->GetPredefinedName()))
			.Set(m_propertyAttributeCode->GetMetaObject(), ibValue(object->GetPredefinedCode()))
			.Set(m_propertyAttributeDescription->GetMetaObject(), ibValue(object->GetPredefinedDescription()))
			.Set(m_propertyAttributeIsFolder->GetMetaObject(), ibValue(object->IsPredefinedFolder()))
			.Set(m_propertyAttributeDeletionMark->GetMetaObject(), ibValue(false))
			.Set(m_propertyAttributeParent->GetMetaObject(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::CreateRaw(this, parent != nullptr ? parent->GetPredefinedGuid() : wxNullGuid)));
	}
}
