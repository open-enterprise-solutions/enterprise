#include "backend/query/schemaSnapshot.h"

#include "backend/query/structureBatch.h"                  // ibStructureBatch + DiffColumnInto
#include "backend/query/schemaBuilder.h"                   // ibSchemaBuilder — the DDL door + FB barrier
#include "backend/query/columnLayout.h"                    // ColumnFieldNames + ibColumnCodec::WriteValue (seed cell spread)
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDropIndex / ibQueryStatement (the seed upsert + uuid delete)
#include "backend/restructureInfo.h"                        // ibRestructureInfo — the apply-change ledger
#include "backend/backend_exception.h"                      // ibBackendCoreException — a refusal stops the apply
#include "backend/query/queryColumn.h"                     // ibBackendQueryColumn::GetName (friendly column name)
#include "backend/query/queryable.h"                        // ibBackendQueryable::GetQueryName / GetQueryTableName
#include "backend/databaseLayer/databaseMaterializeBuilder.h"     // L2-2 RenderMaterialization — derived-state triggers + view
#include "backend/query/derivedStateBuilder.h"              // L3-4 — rebuild derived state after a structure change
#include "appData.h"                                        // db_query — the local channel the seed writes target

ibSchemaMaterialize& ibSchemaMaterialize::Guard(const wxString& expr, const ibQueryPredicatePtr& regenExpr)
{
	// BOTH forms compose, and they must compose together: the trigger reads the text, the regeneration
	// reads the predicate, and a rule kept by one and not the other is two bodies of numbers that
	// disagree with nothing to show for it.
	if (!expr.IsEmpty())
		m_guard = m_guard.IsEmpty() ? expr : (wxT("(") + m_guard + wxT(") AND (") + expr + wxT(")"));
	if (regenExpr)
		m_guardExpr = m_guardExpr
			? ibQueryPredicate::Compose(ibQueryPredicateKind::And, m_guardExpr, regenExpr)
			: regenExpr;
	return *this;
}

wxString ibSchemaMaterialize::SourceTable() const
{
	return m_source != nullptr ? m_source->GetQueryTableName() : wxString();
}

ibMaterializeSpec ibSchemaMaterialize::ToRenderSpec(const wxString& tableName) const
{
	ibMaterializeSpec out;
	out.m_table            = tableName;
	out.m_source           = SourceTable();
	out.m_views            = m_views;   // already in L2-2's vocabulary — nothing to translate
	out.m_periodColumn     = m_periodColumn;
	out.m_periodSourceExpr = m_periodSourceExpr;
	out.m_periodUnit       = m_periodUnit;
	out.m_guard            = m_guard;
	out.m_shards           = m_shards;

	out.m_keyHashColumn    = m_keyHashColumn;

	// A logical column expands to its PHYSICAL fields — a reference dimension is a _RTRef/_RRRef
	// pair, not one column. Reading the layout tier instead of assuming one field per column is
	// what keeps reference dimensions working in the totals key.
	for (const ibBackendQueryColumn* k : m_keys)
		for (const wxString& f : ColumnFieldNames(k))
			out.m_keyColumns.push_back(f);

	for (const ibSchemaDelta& d : m_deltas)
		out.m_deltas.push_back({ d.m_column != nullptr ? d.m_column->GetPhysicalName() : wxString(), d.m_valueExpr });

	return out;
}

void ibDeclareDerivedKey(ibSchemaTable& table, const wxString& tableName,
                         const std::vector<const ibBackendQueryColumn*>& keyCols,
                         ibMetaID hashColumnId)
{
	if (keyCols.empty())
		return;

	const wxString indexName = tableName + wxT("_PK");

	// How wide the key really is — in PHYSICAL fields, which is what an index counts. A reference
	// column is three of them, so a key of seven columns can be an index of twenty-one segments.
	size_t fieldCount = 0;
	for (const ibBackendQueryColumn* col : keyCols)
		fieldCount += ColumnFieldNames(col).size();

	// The key must be UNIQUE: it is what the delta upserts against, and a duplicate would let two rows
	// accumulate half the movements each — totals that are individually plausible and jointly wrong.
	if (db_query == nullptr || !ibKeyNeedsHash(*db_query, fieldCount)) {
		table.Index(indexName, keyCols, /*unique*/ true);
		return;
	}

	// PAST THE ENGINE'S CEILING. The identity moves into one hashed field; the key columns stay
	// exactly as they are, and the delta goes on matching by them (databaseMaterializeBuilder.cpp),
	// so a digest collision can only refuse an insert — never merge two keys into one row.
	// ⚠ THIRTY-TWO, AND THE NUMBER IS LOAD-BEARING. The digest is a 128-bit hash written as hex, so 32
	// characters hold it exactly — and this column is what an INDEX stands on. Left at the default 255
	// it created a VARCHAR(255), which in a UTF8 database is 1020 bytes of declared key and passes
	// Firebird's ceiling (about page_size / 4) on its own: "key size exceeds implementation
	// restriction", with the data never even reaching it.
	const ibBackendQueryColumn* hash =
		table.OwnRaw(ibRawDBColumn::String(KeyHashColumnName(), hashColumnId, 32));
	table.Add(hash);
	table.Index(indexName, { hash }, /*unique*/ true);
	table.m_materialize.m_keyHashColumn = KeyHashColumnName();

	// AND A WAY TO FIND THE ROW. The unique index above answers "is this key already here", not "where
	// is it": the match runs over the key COLUMNS, and an index over a digest cannot serve a comparison
	// of the fields it was made from. Without a second index every movement would scan the whole totals
	// table to find its row — correct, and slower on every posting for the life of the register.
	//
	// The leading columns that fit, greedily: the head of a totals key is the period and the account /
	// the first dimensions, which is where the selectivity is. A key column is taken whole or not at
	// all — half a reference is not a comparison anything can ride.
	//
	// ⚠ ASKED THROUGH L2-2, never read off a dialect from here. A dictionary is the level below's to
	// read; this floor knows that it wants "as many leading columns as an index will hold" and nothing
	// about which engine answers.
	const unsigned int ceiling = ibIndexFieldCapacity(*db_query);
	std::vector<const ibBackendQueryColumn*> lookup;
	size_t used = 0;
	for (const ibBackendQueryColumn* col : keyCols) {
		const size_t width = ColumnFieldNames(col).size();
		if (used + width > ceiling)
			break;
		lookup.push_back(col);
		used += width;
	}
	if (!lookup.empty())
		table.Index(tableName + wxT("_KL"), lookup, /*unique*/ false);
}

const ibSchemaTable* ibSchemaSnapshot::Find(ibMetaID id) const
{
	for (const ibSchemaTable& t : m_tables)
		if (t.m_id == id)
			return &t;
	return nullptr;
}

ibSchemaTable& ibSchemaSnapshot::CreateSchemaTable(const ibBackendQueryable* queryable)
{
	ibSchemaTable t;
	t.m_id        = queryable->GetQueryTableId();
	t.m_name      = queryable->GetQueryTableName();
	t.m_queryable = queryable;
	m_tables.push_back(std::move(t));
	return m_tables.back();
}

ibSchemaTable& ibSchemaSnapshot::Shared(ibMetaID id, const wxString& name)
{
	for (ibSchemaTable& t : m_tables)
		if (t.m_id == id) {
			// Same id, different NAME means two declarations collided in the id space — the caller
			// gets somebody else's table and pours its columns into it, which surfaces much later
			// as a duplicate field or an index over columns nobody added. Ids derived from a metaID
			// must use a HIGH bit: metaIDs are small sequential integers, so a low-bit tweak IS the
			// neighbouring metaobject's id.
			wxASSERT_MSG(t.m_name.IsSameAs(name, false), wxT("schema table id collision: ") + t.m_name + wxT(" vs ") + name);
			return t;
		}
	ibSchemaTable t;
	t.m_id   = id;
	t.m_name = name;
	m_tables.push_back(std::move(t));
	return m_tables.back();
}

namespace {

const ibSchemaColumn* FindColumn(const std::vector<ibSchemaColumn>& cols, ibMetaID id)
{
	for (const ibSchemaColumn& c : cols)
		if (c.m_id == id)
			return &c;
	return nullptr;
}

// (HasIndex removed with the index phases: both halves ask FindIndex now, because both need the
// index ITSELF to compare shapes, not merely to know that the name is taken.)

const ibSchemaIndex* FindIndex(const std::vector<ibSchemaIndex>& indexes, const wxString& name)
{
	for (const ibSchemaIndex& i : indexes)
		if (i.m_name == name)
			return &i;
	return nullptr;
}

// Two same-named indexes are the SAME shape iff their uniqueness and their column list (by model id,
// stable across config instances) match. A mismatch (e.g. Index -> IndexWithAdditionalOrder adds the
// order column, or a unique flip) means the index must be dropped and rebuilt, not left as-is.
bool SameIndex(const ibSchemaIndex& a, const ibSchemaIndex& b)
{
	if (a.m_unique != b.m_unique)             return false;
	if (a.m_columns.size() != b.m_columns.size()) return false;
	for (size_t k = 0; k < a.m_columns.size(); ++k)
		if (a.m_columns[k]->GetColumnId() != b.m_columns[k]->GetColumnId())
			return false;
	return true;
}

// The USER-facing name of a table for the change ledger — the metaobject's name as it reads in the
// metadata tree (e.g. "Enumeration3"), not the physical "ClassNNNN". The queryable vends it directly
// (it already wraps the metaobject); the physical name is the fallback (a table with no queryable).
wxString LedgerName(const ibSchemaTable& t)
{
	return t.m_queryable != nullptr ? t.m_queryable->GetQueryName() : t.m_name;
}

const ibSchemaSeedRow* FindSeedRow(const std::vector<ibSchemaSeedRow>& seed, const wxString& id)
{
	for (const ibSchemaSeedRow& r : seed)
		if (r.m_id == id)
			return &r;
	return nullptr;
}

// A row's cell fingerprint — every (columnId -> value) flattened in column-id order (std::map is sorted),
// so two rows compare equal iff every cell matches. Uses ibValue::GetHashKey() (NOT GetString): for a
// reference cell it keys by the target guid — stable and cheap — whereas GetString would return the
// presentation (unstable, possibly a DB lookup). For primitives GetHashKey is the value's string. An
// enum row has no cells -> empty -> a matched enum never re-writes.
wxString SeedSignature(const ibSchemaSeedRow& row)
{
	wxString s;
	for (const auto& cell : row.m_values)
		s << cell.first << wxT('=') << cell.second.GetHashKey() << wxT('\x1f');
	return s;
}

// Generic seed-row UPSERT — resolve each cell's column by id from the table's own m_columns and SetValue
// it; the uuid is the row key. Captured BY VALUE (queryable + resolved column ptrs live in the config, the
// cells copied) so the closure survives the FB barrier (a row into a just-created table runs post-commit,
// after the ephemeral snapshot is gone). No metadata-specific code — the row is pure data.
void WriteSeedRow(ibStructureBatch& batch, const ibSchemaTable& t, const ibSchemaSeedRow& row, ibDatabaseConnectionHolder* holder)
{
	const wxString    table    = t.m_name;
	const ibMetaData* metaData = t.m_queryable != nullptr ? t.m_queryable->GetMetaData() : nullptr;
	const wxString    uuid     = row.m_id;
	ibDatabaseConnectionHolder* h = holder != nullptr ? holder : db_query->GetHolder();   // the builder's conn, or local

	// The seed's row-key column = the row-identity SCAFFOLD that a UNIQUE index covers (the upsert conflict
	// target), read OFF THE SCHEMA — never a hardcoded "uuid". A table whose key floats over other fields
	// (a register: recorder+line / dimensions) has no unique scaffold key here — but it also carries no
	// predefined seed, so keyName stays empty and the write degrades to a plain insert. Resolved to a
	// string so the deferred lambda holds no pointer into the snapshot.
	wxString keyName;
	for (const ibBackendQueryColumn* sc : t.m_scaffold) {
		for (const ibSchemaIndex& idx : t.m_indexes) {
			if (!idx.m_unique)
				continue;
			bool covers = false;
			for (const ibBackendQueryColumn* ic : idx.m_columns)
				if (ic == sc) { covers = true; break; }
			if (covers) { keyName = sc->GetPhysicalName(); break; }
		}
		if (!keyName.empty())
			break;
	}

	// Resolve each cell to the table's live column by id (a cell for a column the table lacks is dropped).
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> cells;
	for (const auto& cell : row.m_values)
		if (const ibSchemaColumn* c = FindColumn(t.m_columns, cell.first))
			cells.push_back({ c->m_column, cell.second });

	batch.Insert([table, uuid, keyName, cells, metaData, h]() -> bool {
		// A seed row is a VALUE-TABLE row keyed by its scaffold key (the uuid) — UPSERT matching on THAT
		// key, NOT the queryable's data-reference (a seed never sets it, and an enum table has no such
		// column, so matching on it raised "FLDxxxx_TYPE does not belong"). Columns = the key + each
		// cell's physical field spread; cell values bind through the column codec like a real row write.
		const bool hasKey = !keyName.empty();
		std::vector<wxString> columns;
		if (hasKey)
			columns.push_back(keyName);
		for (const auto& cell : cells)
			for (const wxString& f : ColumnFieldNames(cell.first))
				columns.push_back(f);

		ibQueryStatement stmt(ibQueryStatement::Kind::Upsert, table, columns,
			hasKey ? std::vector<wxString>{ keyName } : std::vector<wxString>{}, h);
		int position = 1;
		if (hasKey)
			stmt.SetParamString(position++, uuid);
		for (const auto& cell : cells)
			ibColumnCodec::WriteValue(cell.first, metaData, cell.second, &stmt, position);
		stmt.RunQuery();   // a real failure THROWS; the affected-row count (0 = no change) is not an error
		return true;
	});
}

// Generic seed-row DELETE by uuid.
void EraseSeedRow(ibStructureBatch& batch, const ibSchemaTable& t, const wxString& uuid, ibDatabaseConnectionHolder* holder)
{
	const wxString table = t.m_name;
	ibDatabaseConnectionHolder* h = holder != nullptr ? holder : db_query->GetHolder();
	batch.Insert([table, uuid, h]() -> bool {
		ibQueryStatement del(ibQueryStatement::Kind::Delete, table, { ibRowKeyField() }, {}, h);
		del.SetParamString(1, uuid);
		del.RunQuery();    // a real failure THROWS; deleting a row that isn't there (0 rows) is not an error
		return true;
	});
}

// SEED diff: compare two value tables (baseline vs target) cell-by-cell, poured into the SAME batch as the
// structure so a row seeded into a just-created table defers past the DDL commit (FB barrier):
//   * not in baseline            -> new      -> upsert + "Add value"
//   * in baseline, cells changed -> changed  -> upsert + "Change value"
//   * in baseline, cells equal   -> unchanged-> SKIP (no re-write, no ledger line)
//   * gone from target           -> removed  -> delete + "Remove value"
int DiffSeedInto(ibStructureBatch& batch, const ibSchemaTable* old, const ibSchemaTable& cur, ibRestructureInfo* report, ibDatabaseConnectionHolder* holder)
{
	for (const ibSchemaSeedRow& row : cur.m_seed) {
		const ibSchemaSeedRow* o = (old != nullptr) ? FindSeedRow(old->m_seed, row.m_id) : nullptr;
		if (o != nullptr && SeedSignature(*o) == SeedSignature(row))
			continue;                              // every cell unchanged -> nothing to write

		WriteSeedRow(batch, cur, row, holder);
		if (report != nullptr)
			report->AppendInfo((o != nullptr ? _("Change value ") : _("Add value ")) + row.m_name + _(" in ") + LedgerName(cur));
	}
	if (old != nullptr) {
		for (const ibSchemaSeedRow& orow : old->m_seed) {
			if (FindSeedRow(cur.m_seed, orow.m_id) != nullptr)
				continue;
			EraseSeedRow(batch, cur, orow.m_id, holder);
			if (report != nullptr)
				report->AppendWarning(_("Remove value ") + orow.m_name + _(" from ") + LedgerName(cur));
		}
	}
	return 1;
}

// A column's friendly name for the change ledger (falls back to the physical field if unnamed).
wxString ColName(const ibBackendQueryColumn* col)
{
	if (col == nullptr) return wxEmptyString;
	const wxString name = col->GetName();
	return name.empty() ? col->GetPhysicalName() : name;
}

// CREATE: a table only in target. Scaffold creates it; logical columns are ADDed; indexes follow.
// An EXTERNAL table (sys_const) is created elsewhere — the differ only ADDs its columns, no table/index.
// Reporting: a real table create is ONE ledger line; an external table reports its added columns instead
// (no table line, since it owns no create here).
int CreateTable(ibStructureBatch& batch, const ibSchemaTable& t, ibRestructureInfo* report)
{
	// A TABLE IS BORN WITH ITS SCAFFOLD, AND A TABLE WITHOUT ONE IS BORN WITH ITS OWN COLUMNS. SQL has no
	// `CREATE TABLE x ()` — Firebird answers "Token unknown ... )" and the whole config apply aborts. That is
	// what a REGISTER became when its rowData-blob scaffold was dropped (2026-08-02): identity there floats over
	// the dimensions, it has no row-key column, so the create carried nothing and no new register could be
	// created at all (existing ones were unaffected — they diff through AlterTable). The logical columns then
	// ride the create instead of a follow-up ADD each. `CreateIndex` has guarded its empty case all along.
	bool columnsRodeTheCreate = false;
	if (!t.m_external) {
		std::vector<const ibBackendQueryColumn*> createWith = t.m_scaffold;
		if (createWith.empty()) {
			for (const ibSchemaColumn& c : t.m_columns)
				createWith.push_back(c.m_column);
			columnsRodeTheCreate = true;
		}
		// Still nothing? Then there is no table to speak of — a register declared with no fields at all. Emitting
		// the create anyway is the very statement that fails; skip it and let the NEXT apply create the table once
		// the user gives it a dimension (the differ sees it missing from the baseline and comes back here).
		if (createWith.empty())
			return 1;
		batch.CreateTable(createWith);
		if (report != nullptr)
			report->AppendInfo(_("Create table ") + LedgerName(t));
	}
	for (const ibSchemaColumn& c : t.m_columns) {
		if (!columnsRodeTheCreate)
			DiffColumnInto(batch, c.m_column, nullptr);
		if (report != nullptr && t.m_external)
			report->AppendInfo(_("Add ") + ColName(c.m_column) + _(" to ") + LedgerName(t));
	}
	if (!t.m_external)
		for (const ibSchemaIndex& i : t.m_indexes)
			batch.CreateIndex(i.m_name, i.m_columns, i.m_unique);
	return 1;
}

// ALTER: a table in both. Columns diff by id (DiffColumnInto handles add / drop / type-change); indexes
// diff by name (present-only-in-target -> create, only-in-baseline -> drop). Scaffold never changes.
// Reporting: add / drop is reported unconditionally; a MATCHED column is reported as "Change" ONLY when
// its slot diff actually emitted DDL (StepCount grew) — an unchanged column stays silent.
int AlterTable(ibStructureBatch& batch, const ibSchemaTable& old, const ibSchemaTable& cur, ibRestructureInfo* report, ibSchemaBuilder& schema)
{
	int retCode = 1;

	// ⭐⭐ INDEXES COME DOWN FIRST, COLUMNS SECOND, INDEXES BACK UP LAST — and the order is forced by
	// the engine, not chosen for tidiness. A column that an index stands on cannot be dropped:
	// Firebird answers "column SHARD_ ... is referenced in index ..._PK" and rolls the apply back.
	//
	// Turning split totals OFF is exactly that shape: the shard column leaves the key, so the index
	// changes (drop + recreate) AND the column disappears. Both steps were planned correctly and
	// merely run in the wrong order — the drop of the index sat in the index phase, after the column
	// phase had already failed.
	//
	// ⚠ WHY ONLY THE ACCUMULATION REGISTER SHOWED IT. The accounting register's totals table is
	// re-created wholesale (its key is too wide for an index, so a re-key means DROP TABLE), and a
	// dropped table takes its indexes with it. The accumulation register's key fits, so it is ALTERed
	// in place — and only an ALTER can meet this.
	for (const ibSchemaIndex& o : old.m_indexes) {
		const ibSchemaIndex* c = FindIndex(cur.m_indexes, o.m_name);
		if (c == nullptr || !SameIndex(o, *c)) {
			batch.Ddl(ibDropIndex(o.m_name, cur.m_name));
			if (report != nullptr)
				report->AppendInfo((c == nullptr ? _("Remove index ") : _("Rebuild index ")) + o.m_name);
		}
	}

	for (const ibSchemaColumn& c : cur.m_columns) {
		const ibSchemaColumn* o = FindColumn(old.m_columns, c.m_id);
		const size_t before = batch.StepCount();
		DiffColumnInto(batch, c.m_column, o != nullptr ? o->m_column : nullptr);   // errors THROW now
		if (report != nullptr && batch.StepCount() != before) {   // a step was emitted -> a real change
			if (o == nullptr)
				report->AppendInfo(_("Add ") + ColName(c.m_column) + _(" to ") + LedgerName(cur));
			else
				report->AppendInfo(_("Change ") + ColName(c.m_column) + _(" in ") + LedgerName(cur));
		}
	}
	for (const ibSchemaColumn& o : old.m_columns) {
		if (FindColumn(cur.m_columns, o.m_id) == nullptr) {
			DiffColumnInto(batch, nullptr, o.m_column);   // gone -> drop the whole column (errors THROW)
			if (report != nullptr)
				report->AppendWarning(_("Remove ") + ColName(o.m_column) + _(" from ") + LedgerName(cur));
		}
	}

	// Back up: everything the target declares that is not already standing unchanged. The drops for
	// the changed ones happened in the first phase, so a rebuild is a create here and nothing else.
	for (const ibSchemaIndex& i : cur.m_indexes) {
		const ibSchemaIndex* o = FindIndex(old.m_indexes, i.m_name);
		if (o == nullptr || !SameIndex(*o, i)) {
			batch.CreateIndex(i.m_name, i.m_columns, i.m_unique);
			if (report != nullptr && o == nullptr)
				report->AppendInfo(_("Add index ") + i.m_name);
		}
	}

	return retCode;
}

} // namespace

// Apply a derived table's materialization bundle — drop the old triggers / view, create the new
// ones. Runs AFTER the table's own structure is in place (a trigger references its columns).
//
// The bundle is always replaced WHOLE, never patched: a trigger body carries the column list it
// was generated with, so any structural change to either the movements or the totals invalidates
// it silently. Dropping and recreating is cheap (no data moves — the totals rows are untouched)
// and removes an entire class of "trigger still writing the old column set" bugs.
//
// The DROPs are best-effort: on a first create there is nothing to drop, and not every engine has
// IF EXISTS for every object. A failed drop must not abort an apply, so it is swallowed; a failed
// CREATE is a real error and propagates.
// ...but only when the declaration actually CHANGED, or the installed objects went missing. An
// apply that touched one register used to announce a rebuild for every register in the
// configuration, which reads as "everything was recomputed" and buries the one line that matters.
static void ApplyMaterialization(ibSchemaBuilder& schema, ibDatabaseConnectionHolder* holder,
                                 const ibSchemaTable* old, const ibSchemaTable& t,
                                 ibRestructureInfo* report)
{
	if (!t.m_derived)
		return;

	// Hand the declaration and the connection to L2-2; which dictionaries exist, how the statements
	// are spelled and in what order they run is that level's business. This floor sees no SQL and
	// no dialect — the same way it sees neither for an ordinary DDL statement.
	ibMaterializeSpec was;
	const bool hasOld = (old != nullptr && old->m_derived);
	if (hasOld)
		was = old->m_materialize.ToRenderSpec(old->m_name);

	// ⭐⭐ THROUGH THE BARRIER, LIKE EVERY OTHER READER OF A SHAPE THIS APPLY JUST CHANGED.
	//
	// A trigger body names the columns of the MOVEMENTS table (NEW.fld…_RTRef), and on Firebird a
	// statement cannot see a shape its own transaction created: the table exists, the column exists,
	// and CREATE TRIGGER still answers "Column unknown NEW.FLDnnnn_RTRef". Installed inline, the
	// maintenance failed on every register created from scratch — the one case where the movements
	// table is guaranteed to be new.
	//
	// The barrier already exists for exactly this and was already worded for exactly this ("only its
	// shape was new", schemaBuilder.cpp); it simply had one reader — the seed writes. The bundle is
	// the second. Off a barrier dialect RunOrDefer runs it immediately and nothing changes.
	//
	// ⭐⭐ KEYED BY BOTH TABLES, and keying it by the source alone is what broke the totals switch.
	// The trigger READS the movements — that is why the source was named — but it WRITES the totals
	// table, and the view reads from it. Flipping split totals re-keys the TOTALS table (drop and
	// create) while leaving the movements untouched, so a barrier watching only the source saw
	// nothing new and installed the maintenance inside the very transaction that had just recreated
	// the table it addresses. Both tables are the bundle's shape; both belong in the question.
	// The two-table overload already existed — it was added for the regeneration and this second
	// walker of the same fact was left behind.
	//
	// The declaration is COPIED into the closure: the snapshot is ephemeral and a deferred install
	// runs after the caller has let go of it (same reason the L3-4 regeneration copies).
	const ibMaterializeSpec spec = t.m_materialize.ToRenderSpec(t.m_name);
	const wxString sourceTable = t.m_materialize.SourceTable();

	// The HOLDER travels, not the connection or the builder: a deferred install runs after this
	// function — and after the ibSchemaBuilder that started it — has gone, so a captured reference
	// would dangle. The holder outlives the save and vends the same pinned connection.
	if (!schema.RunOrDefer(sourceTable, t.m_name, [spec, was, hasOld, holder]() {
			ibSchemaBuilder deferred(holder);
			ibApplyMaterialization(deferred.Connection(), spec, hasOld ? &was : nullptr);
			return true;   // a refusal RAISES from L2-2 (docs/exceptions.md §5a)
		}))
		ibBackendCoreException::Error(
			_("Failed to install the totals maintenance for %s - the restructuring was rolled back"),
			LedgerName(t));

	// ⚠ REPORTED ONLY WHEN THE DECLARATION ACTUALLY CHANGED — the intent stated at the top of this
	// function, which had never been carried out. Every apply announced a rebuild for EVERY register
	// in the configuration, so adding an attribute (which no totals table even carries) printed a
	// wall of "Rebuild totals maintenance for ..." and buried the one line that mattered. The bundle
	// is still replaced whole underneath; what changes here is only what the user is told.
	if (report != nullptr) {
		if (!hasOld)
			report->AppendInfo(_("Install totals maintenance for ") + LedgerName(t));
		else if (!ibMaterializationEquivalent(schema.Connection(), was, spec))
			report->AppendInfo(_("Rebuild totals maintenance for ") + LedgerName(t));
	}
}

bool SameStructure(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target)
{
	// No baseline = a fresh database: everything is new, so nothing is the same.
	if (baseline == nullptr)
		return target.Tables().empty();

	// A table on either side that the other does not have is a create or a drop.
	for (const ibSchemaTable& old : baseline->Tables())
		if (!old.m_external && target.Find(old.m_id) == nullptr)
			return false;

	for (const ibSchemaTable& cur : target.Tables()) {
		const ibSchemaTable* old = baseline->Find(cur.m_id);
		if (old == nullptr)
			return false;                                   // new table

		// Columns by MODEL ID, the same key the differ matches on; a column present on one side only is an
		// add or a drop, and a matched pair differs when its type set does — which is exactly the condition
		// DiffColumnInto tests before it emits anything. Adding a type to a COMPOSITE attribute that already
		// carries a reference lands here as "same type set is not same" only when the physical layout really
		// moves; when it does not, the type descriptions compare equal and the table stays unchanged.
		if (old->m_columns.size() != cur.m_columns.size())
			return false;
		for (const ibSchemaColumn& c : cur.m_columns) {
			const ibSchemaColumn* o = FindColumn(old->m_columns, c.m_id);
			if (o == nullptr || o->m_column == nullptr || c.m_column == nullptr)
				return false;
			if (!(o->m_column->GetTypeDesc() == c.m_column->GetTypeDesc()))
				return false;
		}

		// Indexes by name + shape (SameIndex is the differ's own test).
		if (old->m_indexes.size() != cur.m_indexes.size())
			return false;
		for (const ibSchemaIndex& i : cur.m_indexes) {
			const ibSchemaIndex* o = nullptr;
			for (const ibSchemaIndex& cand : old->m_indexes)
				if (cand.m_name == i.m_name) { o = &cand; break; }
			if (o == nullptr || !SameIndex(*o, i))
				return false;
		}

		// A derived table whose maintenance would be rebuilt is database work too.
		if (cur.m_derived != old->m_derived)
			return false;
		if (cur.m_derived && ibDerivedState::NeedsRegeneration(old, cur))
			return false;
	}

	return true;
}

int DiffSnapshots(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder, ibRestructureInfo* report)
{
	int retCode = 1;
	ibSchemaBuilder schema(holder);

	// ── PASS 1: every table's BEFORE event, ahead of the diff itself ────────────────────────────────
	// Deliberately NOT a branch inside the change loop below: a rule can be broken by a change the diff
	// finds NOTHING to do about — lowering a declared limit alters no column, so the loop would never
	// reach that table and the rule would never be asked. The question "may this be applied at all" is
	// therefore asked of every declaration first, before a single statement is emitted.
	//
	// A refusal stops the whole apply, not just its own table: half a structure is not a state anyone
	// asked for. Each refusal has already put its reason in the ledger, and they all run, so the user
	// sees every objection at once instead of one per attempt.
	bool refused = false;
	for (const ibSchemaTable& cur : target.Tables()) {
		if (cur.m_beforeChange && !cur.m_beforeChange(report))
			refused = true;
	}
	if (refused)
		return 0;

	// Tables present in baseline but gone from target -> DROP (a vanished id).
	if (baseline != nullptr) {
		for (const ibSchemaTable& old : baseline->Tables()) {
			if (target.Find(old.m_id) != nullptr)
				continue;
			if (old.m_external)
				continue;                         // external tables (sys_const) are never dropped by the differ

			// A DERIVED table does not own its maintenance: the triggers hang on the MOVEMENTS table
			// and only MENTION the totals by name. Dropping the table therefore leaves them behind,
			// firing on every subsequent write into a table that is gone — the movements stop being
			// writable, and the error surfaces nowhere near the change that caused it.
			//
			// This is the register KIND SWITCH in practice: balances and turnovers keep separate
			// tables under separate ids, so a switch is a drop of one and a create of the other. The
			// old bundle must come down by its OLD name, which is exactly what the old spec holds.
			if (old.m_derived)
				ibDropMaterialization(schema.Connection(), old.m_materialize.ToRenderSpec(old.m_name));

			ibStructureBatch batch(old.m_name);   // a drop needs no metadata
			batch.DropTable();
			if (report != nullptr)
				report->AppendWarning(_("Drop table ") + LedgerName(old));
			batch.Flush(schema);   // errors THROW (caught by the storage's apply try/catch)
		}
	}

	// Tables in target -> CREATE (new id) or ALTER (matched id). One batch per table.
	for (const ibSchemaTable& cur : target.Tables()) {
		const ibSchemaTable* old = baseline != nullptr ? baseline->Find(cur.m_id) : nullptr;


		// ⭐⭐ AND THE MOVEMENTS TABLE HAS DEPENDENTS TOO. The triggers and the view hang on the SOURCE
		// table and name its columns, so a column leaving it cannot go while they stand: Firebird
		// answers "cannot delete COLUMN ... there are 4 dependencies" — one per trigger plus the view —
		// and rolls the whole apply back.
		//
		// The case below covers the mirror image (the TOTALS table being replaced) and was mistaken for
		// the whole story: dropping a dimension or a resource changes the MOVEMENTS, which the totals
		// table may follow without being re-keyed at all. Same rule, other end of the same edge.
		//
		// Nothing is re-installed here: the bundle is replaced whole further down (ApplyMaterialization),
		// and that install is deferred past the DDL commit, so it lands after these columns are gone.
		// ⚠ ASKED OF THE BASELINE, because what has to come down is what is STANDING, and what is
		// standing was declared by the old schema. Walking the TARGET instead misses exactly the case
		// that matters: an accumulation register that switches its kind leaves the previous totals
		// table un-maintained in the new schema, so the target no longer calls it derived — and the
		// triggers that actually hold the movements' columns were never dropped.
		if (!cur.m_derived && old != nullptr && baseline != nullptr) {
			for (const ibSchemaTable& dependent : baseline->Tables()) {
				if (!dependent.m_derived || dependent.m_materialize.SourceTable() != cur.m_name)
					continue;
				ibDropMaterialization(schema.Connection(),
				                      dependent.m_materialize.ToRenderSpec(dependent.m_name));
			}
		}

		// A DERIVED table whose shape changed is REPLACED, never altered.
		//
		// It holds no information of its own — every row is a function of the movements, and the
		// regeneration below recomputes all of them anyway. So migrating it in place buys nothing
		// and costs the whole class of migration edges: a re-keyed row, an index that references a
		// column being dropped, a field the physical table has and the baseline does not. Dropping
		// takes the columns and the indexes with it, and the CREATE that follows is unconditional.
		//
		// The maintenance goes first — the triggers live on the MOVEMENTS table and merely mention
		// this one by name, so they would survive the drop and fire into nothing.
		if (cur.m_derived && old != nullptr && ibDerivedState::NeedsRegeneration(old, cur)) {
			ibDropMaterialization(schema.Connection(), old->m_materialize.ToRenderSpec(old->m_name));
			ibStructureBatch drop(old->m_name);
			drop.DropTable();
			drop.Flush(schema);
			if (report != nullptr)
				report->AppendInfo(_("Rebuild totals table ") + LedgerName(cur));
			old = nullptr;   // from here it is a CREATE, and everything below follows from that
		}

		// ⚠ A TABLE THAT STOPS BEING MAINTAINED WOULD KEEP ITS TRIGGERS — they hang on the SOURCE table
		// and only mention this one by name, so neither the "table vanished" nor the "shape changed"
		// path above covers it. There is deliberately no handling here, because nothing in the tree
		// produces that transition any more: a register declares its derived tables and their
		// maintenance unconditionally, and what varies is the delta's GUARD, not the declaration
		// (accountingRegisterMetadataSchema.cpp). If a future metatype ever does flip Derived() off on
		// a table it keeps, this is where dropping the old bundle belongs.

		// Keyed by the queryable when there IS one — it vends the metadata the field diff needs. A
		// DERIVED table has no metaobject behind it (it is declared BY one, but is not one), so it
		// is keyed by name instead; its columns are raw fields that need no metadata to diff.
		ibStructureBatch batch = (cur.m_queryable != nullptr)
			? ibStructureBatch(cur.m_queryable)
			: ibStructureBatch(cur.m_name);

		// The differ's VERDICT per table, beside the ids that produced it: "create" means the baseline
		// never had this id, "alter" means both sides have it and the columns are about to be compared.

		if (old == nullptr)
			CreateTable(batch, cur, report);
		else
			AlterTable(batch, *old, cur, report, schema);

		// DATA after structure, SAME batch: rows into a just-created table defer past the DDL commit on
		// Firebird; other dialects fill them inside the transaction (ibStructureBatch::Flush -> RunOrDefer).
		DiffSeedInto(batch, old, cur, report, holder);

		batch.Flush(schema);   // errors THROW; a 0-row (DDL / no-change) result is not a failure

		// DERIVED state last: the trigger and view reference the columns the batch just settled, so
		// this cannot run before the flush. Rebuilt on every apply — see ApplyMaterialization for why
		// replacing beats patching. Note it does NOT populate: the table is created empty and the
		// trigger only maintains it from here on, so a table with pre-existing movements needs the
		// L3-4 regeneration pass before its totals mean anything.
		ApplyMaterialization(schema, holder, old, cur, report);

		// L3-4: the bundle above only starts maintaining from NOW. A table created over a source
		// that already holds movements, or one whose grouping just changed, needs its content
		// rebuilt — and an empty totals table is not a neutral state, it reads as "no stock of
		// anything". NeedsRegeneration keeps the common case (a column merely added) free.
		//
		// ⭐⭐ AND IT RUNS THROUGH THE BARRIER DOOR — which is the whole of the rule, and replaces a
		// special case that used to stand here.
		//
		// A rebuild READS the source. On an engine that keeps DDL transactional the source is not
		// readable in the transaction that shaped it — whether this apply CREATED that table or merely
		// ADDED A COLUMN to it. Both refuse at prepare time, and both name the source as if it were the
		// problem: "Table unknown …" for the first, "Column unknown FLD…" for the second — about a
		// column three statements above.
		//
		// There USED to be a condition here instead ("skip when the source is being created in this
		// apply"), and it was a dodge: it silenced one of the two symptoms and left the other armed for
		// the first person to add a dimension. RunOrDefer answers the actual question — is this table
		// durable yet — so the rebuild simply waits for the data phase, the same door the SEED writes go
		// through. On engines without the barrier nothing waits, and a rebuild over a table created
		// empty a moment ago costs one scan of nothing.
		if (ibDerivedState::NeedsRegeneration(old, cur)) {
			// ⚠ A COPY, NOT A REFERENCE. The snapshot is EPHEMERAL — built at save, diffed, discarded —
			// and a deferred rebuild runs after the DDL commit, which is past the point the caller still
			// holds it. The table declaration copies cheaply and safely: its owned columns are
			// shared_ptr and everything else points at the live configuration, which outlives the save.
			const wxString sourceTable = cur.m_materialize.SourceTable();
			// The answer is READ. A deferred rebuild returns true here and reports for real in the
			// post-commit drain (ibSchemaBuilder::Flush), where a failure is a data problem in tables
			// that already exist. One that runs NOW is still inside the apply transaction, so its
			// refusal has somewhere to go — and dropping it left the apply believing the totals had
			// been rebuilt when they had not.
			// BOTH tables gate this: the rebuild reads the movements and writes the totals, and either
			// one being new in this transaction makes it unreadable until the DDL commit.
			if (!schema.RunOrDefer(sourceTable, cur.m_name, [table = cur, holder]() { return ibDerivedState::Regenerate(table, holder); }))
				ibBackendCoreException::Error(
					_("Failed to rebuild the totals for %s - the restructuring was rolled back"),
					LedgerName(cur));
		}
	}

	// ── PASS 3: every table's AFTER event, once the structure is settled ────────────────────────────
	// The pair of the first pass, and deliberately weaker: by now the tables are as declared, so there
	// is nothing left to refuse — this is where an owner SAYS what it did, or does the follow-up work
	// its new shape needs. Still inside the apply transaction, so a rollback takes it along.
	for (const ibSchemaTable& cur : target.Tables()) {
		if (cur.m_afterChange)
			cur.m_afterChange(report);
	}

	return retCode;   // 1 — success; a real DB error THREW (no return-code error signal in the apply path)
}
