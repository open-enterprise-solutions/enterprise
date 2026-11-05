////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the record / register families - the STRUCTURE they declare
//
//	ContributeTables and nothing else: what each family becomes in the database - its table, its
//	columns, its indexes and its seed rows.
//	Split out of commonObjectMetaQuery.cpp, which keeps the QUERY surface (the vended queryables,
//	the value materialisation, the constant resolve). "What does this look like in the database"
//	is one question and now has one file per family, the way accumulationRegisterMetadataSchema.cpp
//	already did for the totals-bearing register.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject - the seed's reference cells
#include "backend/metadataSerialization.h"                        // ibDeserializeValue - a declared value, read with this configuration
#include "backend/system/value/composition/valueComposerField.h"  // ibMaterializeCompositionValue - a declared value made live
#include "backend/system/value/valueGuid.h"                       // ibValueGuid - a declared line's owner, its key
#include "backend/backend_exception.h"                            // ibBackendCoreException - a section that cannot take its lines
#include "backend/query/columnLayout.h"                           // ibOwnerRefColumn / ibFieldSuffix - the scaffold + field names
#include "backend/query/schemaSnapshot.h"                         // ibSchemaSnapshot / ibSchemaTable - what a declaration is
#include "backend/restructureInfo.h"                              // ibRestructureInfo - where a predefined item's refusal is said
#include "backend/databaseLayer/databaseQueryBuilder.h"           // the L2 door - the lines a section's owner already holds
#include "backend/valueInfo.h"                                    // ibReference - a line's owner, read back as stored
#include "core/memory/memoryBuffer.h"                             // ibMemoryBuffer - the owner's bytes

#include <algorithm>   // std::find - an attribute that is itself one of the list order's columns; std::find_if - a line's column
#include <map>         // the lines each owner already holds

// (SnapshotOf removed: building a snapshot is just `common->ContributeTables(snap)` — the metadata side
//  does it directly where it drives the builder, keeping the builder itself config-agnostic.)

namespace {
// One secondary DB index per indexed attribute. Index -> the attribute alone; IndexWithAdditionalOrder ->
// the attribute plus the ORDER A LIST READS THE ROWS IN (`order`): the row reference for an object, (Period,
// Recorder, LineNumber) for a register a recorder writes. So a list filtered or grouped by the attribute
// reads its rows straight off the index, in order. Non-unique — an attribute value repeats. Named
// <table>_<attrId>_IX (metaID key: stable and short). With no list order (a tabular section, a register
// keyed by its dimensions) the ordered variant degrades to a plain index.
//
// ⭐ THE ORDERED VARIANT IS A LIST INDEX, BOTH WAYS. A list sorted by the attribute orders by (attribute,
// order…) and scrolls up in that order reversed; Firebird walks an index forward only, so the ordered
// variant carries its descending twin (ListIndex) — the same as the register's period index.
void ContributeAttributeIndexes(ibSchemaTable& t,
	const std::vector<ibValueMetaObjectAttributeBase*>& attributes,
	const std::vector<const ibBackendQueryColumn*>& order = {})
{
	for (const auto attr : attributes) {
		const ibIndexingMode mode = attr->GetIndexingMode();
		if (mode == ibIndexingMode::ibIndexingMode_DontIndex)
			continue;
		const ibString name = ibString::Format(ibT("%s_%i_IX"), t.m_name, (int)attr->GetColumnId());
		const ibBackendQueryColumn* own = attr->GetQueryColumn();
		if (mode == ibIndexingMode::ibIndexingMode_IndexWithAdditionalOrder && !order.empty()
		    && std::find(order.begin(), order.end(), own) == order.end()) {
			std::vector<const ibBackendQueryColumn*> cols{ own };
			cols.insert(cols.end(), order.begin(), order.end());
			t.ListIndex(name, std::move(cols));
		}
		else
			t.Index(name, { own });
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
		t.Add(object->GetQueryColumn());

	if (const ibValueMetaObjectAttributeBase* refAttr = GetDataReference())
		t.Index(t.m_name + ibT("_REF_UQ"), { refAttr->GetQueryColumn() }, true);               // _REF_UQ — the identity, and the only one

	// Per-attribute secondary indexes (plain + predefined: Code / Description / Parent / ...) —
	// the row reference is the additional-order column. Each carries its own Indexing flag.
	// Same list as the columns above — each attribute carries its own Indexing flag, and a
	// common attribute may be indexed exactly like the object's own.
	ContributeAttributeIndexes(t, GetGenericAttributeArrayObject(), { GetDataReference()->GetQueryColumn() });

	// --- tabular sections — each its own table ---
	for (const auto tab : GetTableArrayObject()) {
		ibSchemaTable& tt = out.CreateSchemaTable(tab->GetQueryable());
		const ibBackendQueryColumn* tabUuid = tt.Scaffold(ibOwnerRefColumn());
		for (const auto object : tab->GetGenericAttributeArrayObject())
			tt.Add(object->GetQueryColumn());
		tt.Index(tt.m_name + ibT("_INDEX"), { tabUuid });                     // tabular uuid index — NOT unique (repeats per owner)
		// A line is its owner and its position — the key a declared line is found and written by (a predefined item's).
		tt.m_seedKey = { tabUuid, tab->GetNumberLine()->GetQueryColumn() };
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
		t.Add(object->GetQueryColumn());

	// The data-reference unique key, as on the record side: each row's own reference, unique per row.
	const ibValueMetaObjectAttributeBase* reference = GetDataReference();
	if (reference != nullptr)
		t.Index(t.m_name + ibT("_REF_UQ"), { reference->GetQueryColumn() }, true);

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
			row.Set(reference->GetQueryColumn(), ibValuePtr<ibValueReferenceDataObject>(
				ibValueReferenceDataObject::Create(this, object->GetGuid(), ibReferenceLoad::OnDemand)));
		if (order != nullptr)
			row.Set(order->GetQueryColumn(), ibValue(position));
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
		t.Add(object->GetQueryColumn());
	for (const auto object : GetDimensionArrayObject())
		t.Add(object->GetQueryColumn());
	for (const auto object : GetResourceArrayObject())
		t.Add(object->GetQueryColumn());
	for (const auto object : GetAttributeArrayObject())
		t.Add(object->GetQueryColumn());

	// The key index: recorder + line for a subordinate register, else the dimension columns
	// (period + dimensions when periodic — GetGenericDimensionArrayObject prepends the period).
	// UNIQUE — these columns ARE the register's key: at most one record per key, same as objects
	// key on a unique uuid. It is the DB-level guarantee against duplicate rows; the app-level
	// ExistData probe alone (a fragile string compare) let dimension-key duplicates slip in.
	std::vector<const ibBackendQueryColumn*> idxCols;
	if (HasRecorder()) {
		idxCols.push_back(GetRegisterRecorder()->GetQueryColumn());
		idxCols.push_back(GetRegisterLineNumber()->GetQueryColumn());
	}
	else {
		for (const auto object : GetGenericDimensionArrayObject())
			idxCols.push_back(object->GetQueryColumn());
	}
	if (!idxCols.empty())
		t.Index(t.m_name + ibT("_INDEX"), idxCols, true);

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
	// ⭐ …AND THE LINE NUMBER LAST, BOTH WAYS: (Period, Recorder, LineNumber) is the register's LIST order,
	// and an index that ends before the order does is one the engine cannot walk. Without it every page of
	// the list sorted the whole register — a million movements, 1100 ms a page scrolling down and the same
	// scrolling up, against 235 ms walked by the index (2026-10-01). Scrolling up reads the order
	// backwards, which Firebird walks only on a descending twin (ListIndex).
	// (Only where the register HAS a Period column — a calculation register is dated otherwise, HasPeriod.)
	std::vector<const ibBackendQueryColumn*> listOrder;
	if (HasRecorder() && HasPeriod() && GetRegisterPeriod() != nullptr && GetRegisterRecorder() != nullptr) {
		listOrder = { GetRegisterPeriod()->GetQueryColumn(), GetRegisterRecorder()->GetQueryColumn(),
			GetRegisterLineNumber()->GetQueryColumn() };
		t.ListIndex(t.m_name + ibT("_PIX"), listOrder);
	}

	// Per-field secondary indexes. Dimensions, resources, attributes and predefined all carry the
	// Indexing flag (each is-a ibValueMetaObjectAttribute), so GetGenericAttributeArrayObject covers
	// them all.
	//
	// ⭐ A DIMENSION OF A REGISTER A RECORDER WRITES RIDES NO KEY. The key above is (Recorder, LineNumber) — the
	// dimensions are in it only where there is no recorder — so a list grouped by a dimension found the rows of
	// one value by walking the list order from its start and testing each: 1.6 s for a page of a rare warehouse
	// among 900 000 movements, and the same again for the rows above it (2026-10-05). The ordered variant is the
	// dimension in that very LIST order, which the page reads straight off.
	ContributeAttributeIndexes(t, GetGenericAttributeArrayObject(), listOrder);
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
	// IsFolder and Parent are set for every kind: where the arrangement has none, its column is switched off
	// (ApplyHierarchyType) and Set passes over a column that is not allowed — the gate the columns are built by.
	//
	// The DATA-REFERENCE cell is the row's OWN reference (guid = its predefined guid = its uuid, this
	// metaID): it (a) makes the predefined value referenceable by its key, and (b) gives each row a UNIQUE
	// _RRRef so the data-reference unique index (_REF_UQ) does not see N rows colliding on the NULL default.
	ibSchemaTable& t = out.Shared(GetMetaID(), GetPhysicalTableName());   // the main table the base just created

	// ⭐ WHAT EVERY PREDEFINED ITEM MUST HAVE, WHATEVER ITS KIND — what it is presented by (DataPresentation: its Code
	// for a chart of accounts, its Description for a catalog), filled in. The table's check before the update starts
	// (m_beforeChange), each refusal into the ledger naming the item; every level adds its own on top of it (a
	// characteristic its Type, an account its analytics).
	t.m_beforeChange += [this](const ibSchemaTable*, ibRestructureInfo* report) -> bool {
		bool passed = true;
		const bool byCode = GetReferencePresentation() == ibDataPresentation_Code;
		for (const ibPredefinedItemDescription& object : m_predefinedDesc.m_items) {
			if (!(byCode ? object.m_code : object.m_description).IsEmpty())
				continue;
			if (report != nullptr)
				report->AppendError(ibString::Format(_("%s.%s: the predefined item has no %s"), GetName(), object.m_name,
					GetDataPresentationAttribute()->GetSynonym()));
			passed = false;
		}
		return passed;
	};

	// ⭐ …AND A CODE AND A DESCRIPTION ARE NOT NARROWED (#646): no shorter, no fewer digits, a code not turned from a string
	// into a number or back. Compared with what the APPLIED configuration declared (`baseline`), never with the rows: the
	// schema is what the base is built from, and rows a change would cut are the rehearsal's to meet (Max, 2026-11-04). A
	// table new to the applied configuration has nothing to narrow.
	t.m_beforeChange += [this](const ibSchemaTable* baseline, ibRestructureInfo* report) -> bool {
		if (baseline == nullptr)
			return true;
		// A length or a count of digits of 0 holds anything.
		const auto narrower = [](unsigned was, unsigned now) { return now != 0 && (was == 0 || now < was); };
		bool passed = true;
		for (const ibValueMetaObjectAttributeBase* attribute : { GetDataCode(), GetDataDescription() }) {
			const ibBackendQueryColumn* const column = attribute->GetQueryColumn();
			const auto applied = std::find_if(baseline->m_columns.begin(), baseline->m_columns.end(),
				[column](const ibSchemaColumn& c) { return c.m_id == column->GetColumnId() && c.m_column != nullptr; });
			if (applied == baseline->m_columns.end())
				continue;
			const ibTypeDescription& was = applied->m_column->GetTypeDesc();
			const ibTypeDescription& now = column->GetTypeDesc();
			const bool number = now.ContainType(ibValueTypes::TYPE_NUMBER);
			const unsigned before = number ? was.GetPrecision() : was.GetLength();   // a number's digits, a string's length
			const unsigned after  = number ? now.GetPrecision() : now.GetLength();
			ibString refusal;
			if (number != was.ContainType(ibValueTypes::TYPE_NUMBER))
				refusal = ibString::Format(_("%s: the %s cannot change between a string and a number"), GetName(), attribute->GetSynonym());
			else if (narrower(before, after))
				refusal = ibString::Format(_("%s: the %s cannot be shorter than it was (%u, now %u)"), GetName(), attribute->GetSynonym(),
					before, after);
			if (refusal.IsEmpty())
				continue;
			if (report != nullptr)
				report->AppendError(refusal);
			passed = false;
		}
		return passed;
	};

	// A code and a description as their field holds them (#646): a value of its type — the door made it one, and one kept
	// from before or under a type since changed is made one here.
	const auto held = [this](const ibValueMetaObjectAttributeBase* attribute, const ibPredefinedCellDescription& declared) {
		return attribute->AdjustValue(declared.IsEmpty() ? ibValue() : ibDeserializeValue(declared.m_value, m_metaData));
	};

	// (No rule on retiring Parent / IsFolder any more, 2026-10-05: a hierarchy switched off takes those
	//  columns the way deleting any attribute takes its own — the list stops declaring them, the differ
	//  drops them and the apply ledger says "Remove …". The rule that refused it kept its own idea of which
	//  columns leave; the list moved without it, and every apply of a catalog without folders died on
	//  "Column unknown" — PR #220.)

	// RAW, never Create - the same rule the enum's seed above states, and this is the site it was
	// written for: a seed cell needs the reference's IDENTITY BYTES, while Create MATERIALISES the
	// object, going off to read the row it points at through the L3 door. That read selects every
	// attribute this metaobject declares - including the one this very apply is about to ADD - so it
	// asks the base for a column that does not exist yet and fails once PER PREDEFINED ITEM, at
	// DECLARATION time, before a single statement has been emitted. ("Field 'fldNNNN_TYPE' not found
	// in the resultset", twelve times over, in front of the real failure.)
	//
	// It is also the schema-authority rule (docs/private/schema-authority.md): what DDL to emit is decided by
	// the two configurations, never by asking the database what it currently holds.
	for (const ibPredefinedItemDescription& object : m_predefinedDesc.m_items) {
		ibSchemaSeedRow& row = t.AddRow(object.m_guid, object.m_name);
		row.Set(GetDataReference()->GetQueryColumn(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::Create(this, object.m_guid, ibReferenceLoad::OnDemand)))
			.Set(m_propertyAttributePredefined->GetMetaObject()->GetQueryColumn(), ibValue(object.m_name))
			.Set(m_propertyAttributeCode->GetMetaObject()->GetQueryColumn(), held(m_propertyAttributeCode->GetMetaObject(), object.m_code))
			.Set(m_propertyAttributeDescription->GetMetaObject()->GetQueryColumn(), held(m_propertyAttributeDescription->GetMetaObject(), object.m_description))
			.Set(m_propertyAttributeIsFolder->GetMetaObject()->GetQueryColumn(), ibValue(object.m_folder))
			.Set(m_propertyAttributeDeletionMark->GetMetaObject()->GetQueryColumn(), ibValue(false))
			.Set(m_propertyAttributeParent->GetMetaObject()->GetQueryColumn(),
				ibValuePtr<ibValueReferenceDataObject>(
					ibValueReferenceDataObject::Create(this, object.m_parent, ibReferenceLoad::OnDemand)));

		// …AND EVERY OTHER VALUE IT DECLARES, into the column of the same id, read with this configuration and a declared
		// reference made live here (ibMaterializeCompositionValue). Only the cells it names: a cell it never declared stays
		// whatever the data holds, as before. A cell of an attribute deleted since is not written - the item still says it,
		// the table no longer has it. What the item must declare is the table's check above, and each kind's on top of it.
		for (const auto& cell : object.m_row)
			if (const ibValueMetaObjectAttributeBase* attribute = FindAnyAttributeObjectByFilter(cell.first))
				row.Set(attribute->GetQueryColumn(),
					ibMaterializeCompositionValue(ibDeserializeValue(cell.second.m_value, m_metaData), m_metaData));

		// ⭐ …AND THE LINES OF THE SECTIONS IT DECLARES, as seed rows of the section's own table, each found by the key
		// every line of a section has: its owner and its position (m_seedKey). The apply diffs them as it diffs the items
		// — a line added or changed; a line no longer declared, and the lines of a section it does not declare, stay the
		// data's.
		for (const auto& declared : object.m_tables) {
			const ibValueMetaObjectTableData* table = FindTableObjectByFilter(declared.first);
			if (table == nullptr)
				continue;
			const ibBackendQueryable* const queryable = table->GetQueryable();   // the one the base declared the section by
			ibSchemaTable& lines = out.Shared(queryable->GetQueryTableId(), queryable->GetQueryTableName());
			if (lines.m_seedKey.empty())
				ibBackendCoreException::Error(_("%s: the declared lines cannot be written - the table names no owner"),
					table->GetName());

			// A cell into its column out of the list the section's table was built from (a chart of accounts' breakdown
			// kinds are columns there, objects of the chart).
			const std::vector<ibValueMetaObjectAttributeBase*> listed = table->GetGenericAttributeArrayObject();
			int number = 1;
			for (const auto& line : declared.second) {
				ibSchemaSeedRow& lineRow = lines.AddRow(wxNullGuid,
					ibString::Format(ibT("%s.%s %i"), object.m_name, table->GetName(), number));
				lineRow.Set(lines.m_seedKey.front(), ibValuePtr<ibValueGuid>(new ibValueGuid(object.m_guid)))
					.Set(table->GetNumberLine()->GetQueryColumn(), ibValue(number));
				for (const auto& cell : line) {
					const auto column = std::find_if(listed.begin(), listed.end(),
						[&cell](const ibValueMetaObjectAttributeBase* attribute) { return attribute->GetMetaID() == cell.first; });
					if (column != listed.end())
						lineRow.Set((*column)->GetQueryColumn(),
							ibMaterializeCompositionValue(ibDeserializeValue(cell.second.m_value, m_metaData), m_metaData));
				}
				++number;
			}
		}
	}

	// ⭐ A DECLARED LINE TAKES ITS PLACE ONLY WHERE NOTHING STANDS. A line is found by its owner and its position, so one the
	// applied configuration did not declare is written over whatever the base holds at that place: a line a person entered
	// after the declared ones, or one the configuration declared once and let go (the apply keeps those). Both are the data's,
	// and nothing would say they were lost; so the section's check before the update refuses, naming the line, for the old
	// ones to be taken out first (Max, 2026-11-02). A section's lines stand at 1..N with no gap (its save numbers them, the
	// seed writes 1..K): the places declared before are the first as many as the baseline declared for the owner, and the
	// next one is taken when the base holds more lines than that.
	for (const auto section : GetTableArrayObject()) {
		const ibBackendQueryable* const queryable = section->GetQueryable();
		ibSchemaTable& lines = out.Shared(queryable->GetQueryTableId(), queryable->GetQueryTableName());
		if (lines.m_seedKey.empty())
			continue;
		lines.m_beforeChange += [this, section, tableName = queryable->GetQueryTableName()](
			const ibSchemaTable* baseline, ibRestructureInfo* report) -> bool {
			// The items that declare more lines here than the applied configuration did, and how many it did.
			std::vector<std::pair<const ibPredefinedItemDescription*, std::size_t>> anew;
			for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items) {
				const auto declared = item.m_tables.find(section->GetMetaID());
				if (declared == item.m_tables.end())
					continue;
				std::size_t before = 0;
				if (baseline != nullptr && !baseline->m_seedKey.empty())
					for (const ibSchemaSeedRow& row : baseline->m_seed) {
						const auto owner = row.m_values.find(baseline->m_seedKey.front()->GetColumnId());
						ibValueGuid* guid = nullptr;
						if (owner != row.m_values.end() && owner->second.ConvertToValue(guid) && guid != nullptr
							&& static_cast<ibGuid>(*guid) == item.m_guid)
							++before;
					}
				if (declared->second.size() > before)
					anew.emplace_back(&item, before);
			}
			if (anew.empty())
				return true;

			ibDatabaseQueryBuilder q;
			if (!q.TableExists(tableName))
				return true;   // nothing has ever been applied here; nothing stands

			std::map<ibGuid, int> held;
			ibQueryIR ir;
			ir.m_root = ibAggregate(ibScan(tableName),
				{ { ibFunc(ibT("COUNT"), { ibCol(ibOwnerRefField()) }), ibT("lineCount") } },
				{ ibCol(ibOwnerRefField()) });
			ibQueryResult rs = q.ExecuteIR(ir);
			while (rs.Next()) {
				ibMemoryBuffer bytes;
				rs.GetResultBlob(ibOwnerRefField(), bytes);
				if (bytes.GetDataLen() >= sizeof(ibReference))
					held[ibGuid(static_cast<const ibReference*>(bytes.GetData())->m_guid)] = rs.GetResultInt(ibT("lineCount"));
			}

			bool passed = true;
			for (const auto& [item, before] : anew) {
				const auto found = held.find(item->m_guid);
				if (found == held.end() || found->second <= static_cast<int>(before))
					continue;
				if (report != nullptr)
					report->AppendError(ibString::Format(
						_("%s.%s %i: the configuration declares this line where the base already holds one - remove the lines held there first"),
						item->m_name, section->GetName(), static_cast<int>(before) + 1));
				passed = false;
			}
			return passed;
		};
	}
}
