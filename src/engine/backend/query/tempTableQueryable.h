#ifndef __TEMP_TABLE_QUERYABLE_H__
#define __TEMP_TABLE_QUERYABLE_H__

// ibTempTableQueryable — a TEMP / pre-filled in-memory table as a FIRST-CLASS L3 source:
// `From(<temp table>)`. It is the proof that a queryable need not be a metaobject
// (docs §22.0): its columns are generic (ibTempColumn — name + type + source-id, NO
// attribute behind them), and it is read through the SAME door + RAM source as a
// register slice — uniformly by GetColumnId(), no attribute, no metaobject. It vends the
// computed provider (its rows are a RAM table it already holds); the composer joins /
// unions it with native sources (catalog / register) like any other leaf. (docs §22.1)

#include "queryProvider.h"                              // ibComputedProvider (vended) + dataQueryBuilder.h / queryable.h
#include "backend/system/value/valueTable.h"           // ibValueModelTable — iterate the pre-filled table's columns

#include <memory>
#include <vector>

// A generic temp-table column — name + type + source-id. NO attribute behind it; the
// source-id (GetColumnId) is the key the RAM table stores the column's value at.
class ibTempColumn : public ibBackendQueryColumn
{
public:
	ibTempColumn(const wxString& name, const ibTypeDescription& type, ibMetaID modelId)
		: m_name(name), m_physical(name), m_type(type), m_modelId(modelId) {}

	// ⭐ THE NAME A QUERY WRITES AND THE NAME THE STORAGE KEEPS ARE TWO DIFFERENT NAMES.
	//
	// For a temp table they are the same string and that is right — its columns are named by whoever
	// made it. A register's TOTALS view is the other case: its columns live in a generated table
	// (`fld1124_D`, `fld1124_D_Week`, `Resource1_Receipt`), and those names reached the constructor's
	// catalogue exactly as stored — so the field list of `Turnovers` read as a dump of a physical
	// schema instead of `Period`, `PeriodWeek`, `Resource1Turnover`.
	//
	// One string became two: the renderer keeps asking for the PHYSICAL name (that is the contract
	// with the generated table, and it must not drift), everybody who shows or resolves a field asks
	// for the ordinary one.
	ibTempColumn(const wxString& name, const wxString& physical,
	             const ibTypeDescription& type, ibMetaID modelId, const wxString& synonym = wxEmptyString,
	             Kind kind = Kind::Composite)
		: m_name(name), m_physical(physical), m_type(type), m_modelId(modelId), m_synonym(synonym),
		  m_kind(kind) {}

	// ⭐ A TEMP TABLE'S COLUMN IS COMPOSITE — it has real fields and spreads into them. A DECLARED
	// query's may not: an output with no column behind it is COMPUTED, one field read by name, and
	// saying so is what stops the reader looking for a `_TYPE` a constant cannot have.
	Kind GetColumnKind() const override { return m_kind; }

	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_physical; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }   // interface returns a non-const ref
	ibMetaID           GetColumnId()      const override { return m_modelId; }

	// ⭐ AND A THIRD NAME: THE ONE A PERSON READS. The two above are for the query and for the
	// storage; neither is a caption. A derived surface publishes figures like `Resource1Balance`,
	// which is a correct NAME and a poor thing to put at the head of a column an accountant reads —
	// and the runtime tables that show these rows used to build their captions separately, which is
	// why the same figure had a presentation through one door and none through the other.
	//
	// Empty = the name, which is the base class's own answer and right for an ordinary temp table:
	// its columns are named by whoever made it, and that name IS the caption.
	wxString           GetSynonym()      const override { return m_synonym.IsEmpty() ? m_name : m_synonym; }

private:
	wxString                  m_name;
	wxString                  m_physical;   // == m_name unless the storage spells it differently
	mutable ibTypeDescription m_type;     // mutable: GetTypeDesc() is const but returns a non-const ref
	ibMetaID                  m_modelId;
	wxString                  m_synonym;    // empty = the name
	Kind                      m_kind = Kind::Composite;   // see GetColumnKind
};

class ibTempTableQueryable : public ibBackendQueryable
{
public:
	// Build from a pre-filled RAM table (an ibValue wrapping ibValueModelTable): derive
	// the generic columns from its collection (name / id / type), each keyed by the SAME
	// id the rows are stored at — so the RAM source reads every column by GetColumnId().
	explicit ibTempTableQueryable(ibValue table) : m_table(std::move(table))
	{
		ibValueModelTable* rows = nullptr;
		if (m_table.ConvertToValue(rows) && rows != nullptr) {
			auto* cols = rows->GetColumnCollection();
			if (cols != nullptr)
				for (unsigned int i = 0; i < cols->GetColumnCount(); ++i) {
					auto* info = cols->GetColumnInfo(i);
					if (info != nullptr)
						m_columns.push_back(std::make_unique<ibTempColumn>(
							info->GetColumnName(), info->GetColumnType(),
							static_cast<ibMetaID>(info->GetColumnID())));
				}
		}
	}

	// A column to reference in Where / OrderBy / GetValue (by its name). Null if absent.
	const ibBackendQueryColumn* Column(const wxString& name) const
	{
		for (const auto& c : m_columns)
			if (c->GetName() == name) return c.get();
		return nullptr;
	}

	// Ownership by object identity (temp columns are not attributes — ResolveAttribute
	// by name is null, so the default OwnsColumn fails; match the column pointer instead).
	bool OwnsColumn(const ibBackendQueryColumn* col) const override
	{
		for (const auto& c : m_columns)
			if (c.get() == col) return true;
		return false;
	}

	// Temp columns aren't attributes — resolve a UNION branch's column by name here.
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return Column(name); }

	// All exposed columns — for SELECT * over this temp source (nested subquery).
	std::vector<const ibBackendQueryColumn*> GetColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> out;
		out.reserve(m_columns.size());
		for (const auto& c : m_columns) out.push_back(c.get());
		return out;
	}

	// A RAM-held relation: computed-in-RAM, like ibSubqueryQueryable. The composer's
	// co-location gate keys on this (a temp source has no physical table name, so it must
	// NOT be treated as a co-locatable DB leaf); when joined with a DB source it is
	// temp-promoted (PromoteComputedLeaf) instead. Single-source reads use GetProvider().
	bool IsComputedInRam() const override { return true; }

	// --- the temp table VENDS the computed (RAM) provider; rows = the held table ----
	ibBackendQueryProvider& GetProvider() const override
	{
		static ibComputedProvider s_computedProvider;   // stateless — reads the table from ComputeRows
		return s_computedProvider;
	}
	// Convert the held runtime table into L3's own ibQueryRamTable at this ingest seam
	// (this bridge is the one place a runtime table enters L3). Keyed by each column's
	// source-id (GetColumnId) — the RAM source then reads every column by id.
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override
	{
		ibQueryRamTable t;
		for (const auto& c : m_columns)
			t.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());
		ibValueModelTable* rows = nullptr;
		if (m_table.ConvertToValue(rows) && rows != nullptr) {
			const long n = rows->GetRowCount();
			for (long i = 0; i < n; ++i) {
				const long r = t.AppendRow();
				for (const auto& c : m_columns) {
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
	wxString GetQueryTableName() const override { return wxEmptyString; }
	ibGuid GetQueryTableGuid() const override { return wxNullGuid; }
	ibMetaID GetQueryTableId()    const override { return 0; }

private:
	ibValue                                    m_table;     // owns the pre-filled rows
	std::vector<std::unique_ptr<ibTempColumn>> m_columns;   // generic columns, keyed by source-id
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
// key. (docs/temp-db.md)
// ==========================================================================
class ibDbTempTableQueryable : public ibBackendQueryable
{
public:
	ibDbTempTableQueryable(wxString tableName, std::vector<ibTempColumn> columns, const ibMetaData* metaData = nullptr)
		: m_tableName(std::move(tableName)), m_tableGuid(wxNewUniqueGuid), m_columns(std::move(columns)), m_metaData(metaData) {}

	wxString          GetQueryTableName() const override { return m_tableName; }
	ibGuid          GetQueryTableGuid() const override { return m_tableGuid; }
	ibMetaID          GetQueryTableId()    const override { return 0; }                 // not a metaobject
	const ibMetaData* GetMetaData()       const override { return m_metaData; }        // reference / enum reconstruction context

	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override
	{
		for (const ibTempColumn& c : m_columns)
			if (c.GetName() == name) return &c;
		return nullptr;
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> out;
		out.reserve(m_columns.size());
		for (const ibTempColumn& c : m_columns) out.push_back(&c);
		return out;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override
	{
		for (const ibTempColumn& c : m_columns)
			if (&c == col) return true;
		return false;
	}
	// GetProvider NOT overridden → inherits the DB default (ibDbTableProvider): the temp table is
	// read by an ordinary physical scan. That is the whole point — it is just another DB source.

private:
	wxString                  m_tableName;   // the real temp table name (the manager owns its DB lifetime)
	ibGuid					  m_tableGuid;   // the real temp table GUID (the manager owns its DB lifetime)	
	std::vector<ibTempColumn> m_columns;     // metadata-format columns (real type), read via the DB spread
	const ibMetaData*         m_metaData;    // reference / enum reconstruction context
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
// configuration could not be applied at all. Everything that needed the key then composed one out of
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
	ibSchemaTableQueryable(wxString tableName, ibMetaID tableId,
	                       std::vector<const ibBackendQueryColumn*> columns,
	                       const ibMetaData* metaData = nullptr,
	                       std::vector<const ibBackendQueryColumn*> keyColumns = {})
		: m_tableName(std::move(tableName)), m_tableGuid(wxNewUniqueGuid), m_tableId(tableId)
		, m_columns(std::move(columns)), m_metaData(metaData), m_keyColumns(std::move(keyColumns)) {}

	std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return m_keyColumns; }

	wxString          GetQueryTableName() const override { return m_tableName; }
	ibGuid            GetQueryTableGuid() const override { return m_tableGuid; }
	ibMetaID          GetQueryTableId()   const override { return m_tableId; }
	const ibMetaData* GetMetaData()       const override { return m_metaData; }
	// No row key and no keyset: a derived table is addressed by its declared key columns, never
	// scrolled. An identity sort would invent an ordering nothing stores.

	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override
	{
		for (const ibBackendQueryColumn* c : m_columns)
			if (c->GetName() == name) return c;
		return nullptr;
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_columns; }
	bool OwnsColumn(const ibBackendQueryColumn* col) const override
	{
		for (const ibBackendQueryColumn* c : m_columns)
			if (c == col) return true;
		return false;
	}

private:
	wxString                                 m_tableName;
	ibGuid                                   m_tableGuid;
	ibMetaID                                 m_tableId;
	std::vector<const ibBackendQueryColumn*> m_columns;    // NOT owned — the schema table / the config own them
	const ibMetaData*                        m_metaData;   // reference / enum reconstruction context
	std::vector<const ibBackendQueryColumn*> m_keyColumns; // what makes a row unique — the declared key
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
// (docs/query-language-arc.md §24.4 — result links)
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
	// Non-owning, like every metadata column a query holds: it belongs to the metaobject and outlives
	// the run (the same rule ibSubqueryQueryable states for the columns it publishes but did not
	// allocate).
	struct Field {
		wxString                    m_name;
		wxString                    m_physical;
		ibTypeDescription           m_type;
		ibBackendQueryColumn::Kind  m_kind = ibBackendQueryColumn::Kind::Composite;
		const ibBackendQueryColumn* m_borrowed = nullptr;   // published as-is; the four above are then unused
	};

	// `firstColumnId` — the id the minted columns are numbered from. A CTE's columns stand for
	// nothing stored, so their ids exist only to tell them apart; the caller passes a base out of the
	// synthetic range it already uses, so two named queries in one statement cannot collide.
	ibCteQueryable(wxString name, const std::vector<Field>& fields, ibMetaID firstColumnId,
	               const ibMetaData* metaData = nullptr)
		: m_name(std::move(name)), m_guid(wxNewUniqueGuid), m_metaData(metaData)
	{
		ibMetaID id = firstColumnId;
		for (const Field& field : fields) {
			if (field.m_borrowed != nullptr) {
				// Published as it stands — its id, its type and its layout are the source's, which is
				// what makes it readable at all: the fields it names are the ones the SELECT wrote for
				// the columns it is made of.
				m_columns.push_back(field.m_borrowed);
				continue;
			}
			if (field.m_name.IsEmpty())
				continue;   // a field with no name cannot be read back by one
			// The name IS the physical name: a CTE exposes exactly the aliases its select wrote.
			m_owned.push_back(std::make_shared<ibTempColumn>(field.m_name,
				field.m_physical.IsEmpty() ? field.m_name : field.m_physical, field.m_type, id++,
				wxEmptyString, field.m_kind));
			m_columns.push_back(m_owned.back().get());
		}
	}

	// The storage, so a schema that names one of these columns keeps it alive past the run — the
	// same contract the nested-subquery wrapper answers (queryable.h ShareColumn).
	std::shared_ptr<ibBackendQueryColumn> ShareColumn(const ibBackendQueryColumn* col) const override
	{
		for (const std::shared_ptr<ibTempColumn>& c : m_owned)
			if (c.get() == col) return c;
		return nullptr;
	}

	wxString          GetQueryTableName() const override { return m_name; }
	ibGuid            GetQueryTableGuid() const override { return m_guid; }
	ibMetaID          GetQueryTableId()   const override { return 0; }          // not a metaobject
	const ibMetaData* GetMetaData()       const override { return m_metaData; }

	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override
	{
		for (const ibBackendQueryColumn* c : m_columns)
			if (c != nullptr && c->GetName() == name) return c;
		return nullptr;
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_columns; }
	bool OwnsColumn(const ibBackendQueryColumn* col) const override
	{
		for (const ibBackendQueryColumn* c : m_columns)
			if (c == col) return true;
		return false;
	}

private:
	wxString                                 m_name;
	ibGuid                                   m_guid;
	std::vector<std::shared_ptr<ibTempColumn>> m_owned;    // the columns this source publishes — minted here, SHARED so a reader may keep one
	std::vector<const ibBackendQueryColumn*> m_columns;    // …and the same ones as the interface hands them out
	const ibMetaData*                        m_metaData;
};

#endif // __TEMP_TABLE_QUERYABLE_H__
