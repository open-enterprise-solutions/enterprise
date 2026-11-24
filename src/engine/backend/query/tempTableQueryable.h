#ifndef __TEMP_TABLE_QUERYABLE_H__
#define __TEMP_TABLE_QUERYABLE_H__

// THE SOURCES NO METAOBJECT DECLARES — a table in memory, a temp table in the base, a table the schema
// declared (a register's totals), a query the statement named (WITH). A caller builds one and hands it to
// From() / Join(); which provider reads it is the source's own answer (GetProvider), so they are the face's,
// not a provider's. The first reads itself in memory; the other three are a NAME in SQL with declared
// columns, read by the database provider's ordinary physical scan.

#include "backend/query/queryProvider.h"                // dataQueryBuilder.h / queryable.h (ibComputedProviderInstance, vended)
#include "backend/system/value/valueTable.h"           // ibValueModelTable — iterate the pre-filled table's columns
#include "backend/query/tempColumn.h"                  // ibTempColumn — their columns

#include <memory>
#include <vector>

// ==========================================================================
// ibTempTableQueryable — a TEMP / pre-filled in-memory table as a FIRST-CLASS L3 source:
// `From(<temp table>)`. It is the proof that a queryable need not be a metaobject
// (docs §22.0): its columns are generic (ibTempColumn — name + type + source-id, NO
// attribute behind them), and it is read through the SAME door + RAM source as a
// register slice — uniformly by GetColumnId(), no attribute, no metaobject. It vends the
// computed provider (its rows are a RAM table it already holds); the composer joins /
// unions it with native sources (catalog / register) like any other leaf. (docs §22.1)
// ==========================================================================
class ibTempTableQueryable : public ibBackendQueryable
{
public:
	// Build from a pre-filled RAM table (an ibValue wrapping ibValueModelTable): derive
	// the generic columns from its collection (name / id / type), each keyed by the SAME
	// id the rows are stored at — so the RAM source reads every column by GetColumnId().
	// `metaData` — the application its references belong to (see GetMetaData).
	explicit ibTempTableQueryable(ibValue table, const ibMetaData* metaData = nullptr)
		: m_table(std::move(table)), m_metaData(metaData)
	{
		ibValueModelTable* rows = nullptr;
		if (m_table.ConvertToValue(rows) && rows != nullptr) {
			auto* cols = rows->GetColumnCollection();
			if (cols != nullptr)
				for (unsigned int i = 0; i < cols->GetColumnCount(); ++i) {
					auto* info = cols->GetColumnInfo(i);
					if (info != nullptr)
						m_columns.push_back(std::make_shared<ibTempColumn>(
							info->GetColumnName(), info->GetColumnType(),
							static_cast<ibMetaID>(info->GetColumnID()), metaData));
				}
		}
	}

	// A column to reference in Where / OrderBy / GetValue (by its name). Null if absent.
	std::shared_ptr<const ibBackendQueryColumn> Column(const ibString& name) const
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			if (c->GetName() == name) return c;
		return nullptr;
	}

	// Ownership by object identity (temp columns are not attributes — ResolveAttribute
	// by name is null, so the default ContainColumn fails; match the column instead).
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			if (c == col) return true;
		return false;
	}

	// All exposed columns — for SELECT * over this temp source (nested subquery), and a UNION branch's
	// column by name (ResolveColumnByName searches this list).
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_columns; }

	// --- the temp table VENDS the computed provider; rows = the held table ----
	// A relation held in memory, like ibSubqueryQueryable: no statement of the base reads it, so a
	// read of it — alone or joined — copies its rows into a base in memory and is read there
	// (ibQueryComposer::OverMemorySpace).
	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }   // reads the table from ComputeRows
	// Convert the held runtime table into L3's own ibQueryRamTable at this ingest seam
	// (this bridge is the one place a runtime table enters L3). Keyed by each column's
	// source-id (GetColumnId) — the RAM source then reads every column by id.
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override
	{
		ibQueryRamTable t;
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			t.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());
		ibValueModelTable* rows = nullptr;
		if (m_table.ConvertToValue(rows) && rows != nullptr) {
			const long n = rows->GetRowCount();
			for (long i = 0; i < n; ++i) {
				const long r = t.AppendRow();
				for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns) {
					ibValue v;
					rows->GetValueByMetaID(rows->GetItem(i), static_cast<unsigned int>(c->GetColumnId()), v);
					t.SetCell(r, c->GetColumnId(), v);
				}
			}
		}
		return t;
	}

	// --- queryable interface — trivial for a non-metaobject temp source ------------
	// (No attribute resolution / DB-row materialisation here — a temp source is computed
	//  in RAM, so those concerns do not exist; the base interface names none of them.)
	ibString GetQueryTableName() const override { return ibEmptyString; }
	ibMetaID GetQueryTableId()    const override { return 0; }

	// ⭐ THE APPLICATION ITS REFERENCES BELONG TO — handed in by whoever wraps the table (the query it is
	// read by, the Data unit, the other side of a join). With none, a reference column could not say which
	// catalog it points at, and «IN HIERARCHY» over it stood for the named values alone (2026-09-29).
	const ibMetaData* GetMetaData() const override { return m_metaData; }

private:
	ibValue                                    m_table;     // owns the pre-filled rows
	const ibMetaData*                          m_metaData = nullptr;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_columns;   // generic columns (ibTempColumn), keyed by source-id
};


// ==========================================================================
// ibDbTempTableQueryable — a DB TEMPORARY table as a first-class L3 source: From(tempSource). The
// PHYSICAL counterpart of ibTempTableQueryable (which is RAM): it names a REAL temp table and is
// read through the ORDINARY DB provider (it does NOT override GetProvider — it inherits the DB
// default, ibDbTableProvider), so a materialised intermediate joins SERVER-SIDE, no RAM composer.
// The temp-table MANAGER creates + fills the table, then hands its name + the column descriptors
// here; the queryable is a thin handle (the manager owns the DB lifetime via its pinning scope).
//
// Columns are METADATA-FORMAT (ibTempColumn — name + real type descriptor + source-id, NOT raw): the
// temp table stores each column in the SAME physical spread a real table uses (TYPE + per-type data +
// _RTRef/_RRRef), so the ordinary DB read (GetValueColumn over the spread + this queryable's metaData)
// reconstructs reference / enum / variant values from a temp EXACTLY like from a real table — keys AND
// outputs. The manager fills the spread via SetValueColumn. Read-only scan source: no keyset, no write
// key. (docs/private/temp-db.md)
// ==========================================================================
class ibDbTempTableQueryable : public ibBackendQueryable
{
public:
	// The columns come as descriptions their builder assembled; each is published held, here.
	ibDbTempTableQueryable(ibString tableName, std::vector<ibTempColumn> columns, const ibMetaData* metaData = nullptr)
		: m_tableName(std::move(tableName)), m_tableGuid(ibNewUniqueGuid), m_metaData(metaData)
	{
		m_columns.reserve(columns.size());
		for (ibTempColumn& c : columns)
			m_columns.push_back(std::make_shared<ibTempColumn>(std::move(c)));
	}
	// …A COPY OF A SOURCE (ibTempTableManager::Materialise of a source): the source's own columns and key, published as
	// they are — a query built over the source reads the copy with the columns it already holds, their names, types and
	// what they stand for included.
	ibDbTempTableQueryable(ibString tableName, std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns,
	                       std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys, const ibMetaData* metaData)
		: m_tableName(std::move(tableName)), m_tableGuid(ibNewUniqueGuid), m_columns(std::move(columns))
		, m_metaData(metaData), m_keys(std::move(keys)) {}

	ibString          GetQueryTableName() const override { return m_tableName; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_tableGuid; }
	ibMetaID          GetQueryTableId()    const override { return 0; }                 // not a metaobject
	const ibMetaData* GetMetaData()       const override { return m_metaData; }        // reference / enum reconstruction context

	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_columns; }
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return m_keys; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			if (c == col) return true;
		return false;
	}
	// GetProvider NOT overridden → inherits the DB default (ibDbTableProvider): the temp table is
	// read by an ordinary physical scan. That is the whole point — it is just another DB source.

private:
	ibString                  m_tableName;   // the real temp table name (the manager owns its DB lifetime)
	ibUniqueKey				  m_tableGuid;   // the real temp table GUID (the manager owns its DB lifetime)
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_columns;   // metadata-format columns (real type), read via the DB spread
	const ibMetaData*         m_metaData;    // reference / enum reconstruction context
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_keys;      // a copy's: its source's key; none otherwise
};

// ==========================================================================
// ibSchemaTableQueryable — a table the SCHEMA declared, read and written as an ordinary L3 source.
//
// The case it exists for is the DERIVED table (a register's totals). Every other table in the
// snapshot is declared BY a metaobject and therefore already has that metaobject's queryable; a
// derived table is declared by one but is not one, so nothing vends a source for it — and until
// something did, both L3-4 floors were unreachable: the door needs a queryable to read or write
// through, and their entry gates simply returned "nothing to do" on a null.
//
// Physically it is an ordinary table, so it needs no provider of its own — like ibDbTempTableQueryable
// it does NOT override GetProvider and is read by the plain DB scan. What it does need is the metaData
// context, because a totals table's dimensions are real attribute columns (a reference dimension is a
// field spread), so reconstructing their values takes the same machinery any other table's read does.
//
// ⭐ IT KNOWS ITS OWN KEY, because the schema that declared the table hands it over — the very list
// the unique index is built from (ibDeclareDerivedKey). A derived table used to report none, which
// left an upsert with nothing to match on and the statement came out as `MATCHING ()`: a
// application could not be applied at all. Everything that needed the key then composed one out of
// the parts it could see (a period here, a shard column found by name there), which is three copies
// of one fact and two ways for them to disagree.
//
// An UPDATE is unaffected by this: it matches on the key columns it actually WRITES, and the door's
// own .Where() always applies — so the shard fold, which writes only the accumulating columns and
// pins one physical row by Where, addresses exactly the row it did before.
// ==========================================================================
class ibSchemaTableQueryable : public ibBackendQueryable
{
public:
	ibSchemaTableQueryable(ibString tableName, ibMetaID tableId,
	                       std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns,
	                       const ibMetaData* metaData = nullptr,
	                       std::vector<std::shared_ptr<const ibBackendQueryColumn>> keyColumns = {})
		: m_tableName(std::move(tableName)), m_tableGuid(ibNewUniqueGuid), m_tableId(tableId)
		, m_columns(std::move(columns)), m_metaData(metaData), m_keyColumns(std::move(keyColumns)) {}

	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return m_keyColumns; }

	ibString          GetQueryTableName() const override { return m_tableName; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_tableGuid; }
	ibMetaID          GetQueryTableId()   const override { return m_tableId; }
	const ibMetaData* GetMetaData()       const override { return m_metaData; }
	// No row key and no keyset: a derived table is addressed by its declared key columns, never
	// scrolled. An identity sort would invent an ordering nothing stores.

	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_columns; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			if (c == col) return true;
		return false;
	}

private:
	ibString                                 m_tableName;
	ibUniqueKey                              m_tableGuid;
	ibMetaID                                 m_tableId;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_columns;    // shared with the schema table / the application
	const ibMetaData*                        m_metaData;   // reference / enum reconstruction context
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_keyColumns; // what makes a row unique — the declared key
};

// ==========================================================================
// ibCteQueryable — A QUERY THIS STATEMENT NAMED, read as if it were a table.
//
// The third member of this family, and the same idea as the two above: a source that is a NAME in
// SQL with declared columns, read by the ordinary physical scan (GetProvider is NOT overridden). It
// differs from a temp table in WHO makes the name exist — nobody creates anything: the statement
// DECLARES it with `WITH <name> AS (…)`, and the door carries the inner query (ibDataQueryBuilder::With)
// so the provider can write that declaration into the same IR.
//
// ⭐ WHY IT IS NOT ibSubqueryQueryable. That one is COMPUTED IN RAM by construction — the inner
// query runs, its rows come back to us, and everything above joins them here. That is the right
// answer for a nested query over a source the DBMS cannot see, and the wrong one for the case this
// class exists for: a named result of the SAME package, on the SAME connection, which the server can
// read itself. Same shape, opposite execution — so they are two classes and not one with a flag.
//
// The columns are the inner query's OUTPUT columns, shared (not copied): the inner door published
// them and outlives the read through the builder the CTE holds.
// (docs/private/query-language-arc.md §24.4 — result links)
// ==========================================================================
class ibCteQueryable : public ibBackendQueryable
{
public:
	// WHAT THE NAMED QUERY PUBLISHES — one entry per output field: the name it is read by and the
	// type it holds. Both come from the inner query's own output schema, which is the only honest
	// source for them: a CTE has no storage to ask.
	// ⭐ TWO NAMES, and the second is what keeps a declaration collision-free. `m_name` is what a query
	// WRITES (`Sales.PointInTime`); `m_physical` is what the fields are spelled from — and it comes
	// from the source column, so it is `fld<metaID>`: unique per metatype by construction. Two
	// documents joined into one declaration therefore spread into fld1672_* and fld9001_*, instead of
	// two sets of PointInTime_* that no engine would accept.
	// ⭐ …AND WHAT KIND OF COLUMN IT IS. A field over a real column spreads the way that column does;
	// one over an EXPRESSION is `Computed` — a single field read by name — because that is exactly
	// what the declaration's own SELECT wrote for it. Getting this wrong is not a preference: the
	// reader then hunts for a `_TYPE` the statement never carried.
	// ⭐⭐ …OR THE SOURCE'S OWN COLUMN, HANDED OVER RATHER THAN MINTED. A SYNTHETIC column — the
	// document's MOMENT — has no field of its own: it is read out of the date and the reference, and
	// those two ARE published, under their own physical names. Minting a `PointInTime` field for it
	// would name a field the SELECT never wrote (`-206`), so the declaration publishes THE COLUMN
	// ITSELF: it already knows how to read itself out of the fields the statement carries.
	//
	// 🛑 IT WAS DROPPED INSTEAD, and the moment then did not exist outside the declaration:
	// `unknown attribute 'PointInTime' on source 'q_sub0'` when a report grouped by it, and — worse,
	// because nothing was raised — a column that simply vanished from the output when it did not.
	// "What to write into the SELECT" and "what may be named from outside" are two questions; a
	// synthetic column answers NOTHING to the first and its own name to the second.
	//
	// Held, like every column a query holds: a share of the metaobject's own.
	struct Field {
		ibString                    m_name;
		ibString                    m_physical;
		ibTypeDescription           m_type;
		ibBackendQueryColumn::Kind  m_kind = ibBackendQueryColumn::Kind::Composite;
		std::shared_ptr<const ibBackendQueryColumn> m_borrowed;   // published as-is; the four above are then unused
		// ⭐ …AND WHETHER THIS FIELD IS THE ROW ITSELF — the inner source's uniqueness key, carried
		// across the declaration. A CTE has no storage to ask, so the only honest moment to know it is
		// where the declaration is built and the inner source is still in hand.
		bool                        m_isKey = false;
		// …and whether the inner source's rows are ordered by it (GetSortColumn) — what a sort by the row's own
		// reference sorts by, carried across the same way.
		bool                        m_isSort = false;
		// …and what it is in a balance, carried across the declaration as the type is: a register's period
		// read one level down is still the moment its totals are taken at.
		ibBalanceRole               m_balanceRole = ibBalanceRole::None;
		int                         m_periodRank  = 0;
	};

	// `firstOrdinal` — the ORDINARY number the minted columns are counted from, one per declaration
	// (its place among this run's named queries, times its block). A CTE's columns stand for nothing
	// stored, so their ids exist only to tell them apart.
	//
	// 🛑 THE ORDINAL ADVANCES, THE ID IS COMPOSED — the rule queryColumn.h states over SyntheticId,
	// and this is where it was broken: the caller handed in an ALREADY COMPOSED base and the loop ran
	// `id++` over it. Composed ids do not sit next to each other — `base + 1` is the same value under
	// ANOTHER KIND — so the first declaration's columns came out as Subquery(0), GroupKey(0),
	// Aggregate(0), Stitch(0), Alias(0), Output(0), and then 0, 1, 2… : from the seventh field on,
	// POSITIVE ids, indistinguishable from the metaIDs of declared attributes.
	ibCteQueryable(ibString name, const std::vector<Field>& fields, ibMetaID firstOrdinal,
	               const ibMetaData* metaData = nullptr)
		: m_name(std::move(name)), m_guid(ibNewUniqueGuid), m_metaData(metaData)
	{
		ibMetaID ordinal = firstOrdinal;
		for (const Field& field : fields) {
			if (field.m_borrowed != nullptr) {
				// Published as it stands — its id, its type and its layout are the source's, which is
				// what makes it readable at all: the fields it names are the ones the SELECT wrote for
				// the columns it is made of.
				m_columns.push_back(field.m_borrowed);
				if (field.m_isKey) m_keys.push_back(field.m_borrowed);
				if (field.m_isSort) m_sort = field.m_borrowed;
				continue;
			}
			if (field.m_name.IsEmpty())
				continue;   // a field with no name cannot be read back by one
			// The name IS the physical name: a CTE exposes exactly the aliases its select wrote.
			const std::shared_ptr<ibTempColumn> column = std::make_shared<ibTempColumn>(field.m_name,
				field.m_physical.IsEmpty() ? field.m_name : field.m_physical, field.m_type,
				ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Subquery, ordinal++),
				metaData, ibEmptyString, field.m_kind);
			column->PlaysInBalance(field.m_balanceRole, field.m_periodRank);
			m_columns.push_back(column);
			if (field.m_isKey) m_keys.push_back(column);
			if (field.m_isSort) m_sort = column;
		}
	}

	ibString          GetQueryTableName() const override { return m_name; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_guid; }
	ibMetaID          GetQueryTableId()   const override { return 0; }          // not a metaobject
	const ibMetaData* GetMetaData()       const override { return m_metaData; }

	// ⭐⭐ WHICH COLUMN IS THE ROW — carried across the declaration instead of being lost at it.
	//
	// 🛑 THIS ANSWERED NOTHING, and the silence reached every report. A COMPOSITION always renders its
	// source as a nested query (queryLowering.cpp, the note over `q_sub<n>`), and that nested query is
	// declared as one of these — so `ibQueryComposer`'s `DimCtx::identity` came back 0 for every
	// composed report there has ever been. A level keyed by the row's own identity then stopped BEING
	// the row: a catalog grouped by Reference printed its headings with Code and Description BLANK,
	// which is the exact defect `AttachDimValue`'s rule exists to prevent. Measured 2026-09-09 on a
	// live base: source `q_sub0` keys=0, against `Goods` keys=1 on the road that does not wrap.
	//
	// ⚠ It is the DECLARATION's own columns that are published, not the inner source's — the same
	// shape `ibAliasQueryable` answers with, and for the same reason: a wrapper is the same rows under
	// new column identities, so the answer has to be in the identities the outer query will see.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return m_keys; }
	// …AND WHAT THE ROWS ARE ORDERED BY, carried as the key is: a journal's own reference sorted by its kind's order
	// asked the declaration for a document's fields it does not write (`-206 Column unknown FLD1202_D`, 2026-11-24).
	// Null when the inner order is not among the outputs — a document's, which is read out of its parts.
	std::shared_ptr<const ibBackendQueryColumn> GetSortColumn() const override { return m_sort; }

	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_columns; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
			if (c == col) return true;
		return false;
	}

private:
	ibString                                 m_name;
	ibUniqueKey                              m_guid;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_columns;    // the columns this source publishes — minted here or the source's own
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_keys;       // …and the subset of them that IS the row (see GetPrimaryKeyColumns)
	std::shared_ptr<const ibBackendQueryColumn>              m_sort;       // …and the one they are ordered by (see GetSortColumn)
	const ibMetaData*                        m_metaData;
};

#endif // __TEMP_TABLE_QUERYABLE_H__
