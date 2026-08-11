#ifndef __DATABASE_MATERIALIZE_BUILDER_H__
#define __DATABASE_MATERIALIZE_BUILDER_H__

// L2-2 — the MATERIALIZATION renderer. The second half of level 2.
//
// L2-1 is "a structured description in, dialect-spelled SQL out", and it has two halves:
//   L2-1  ibQueryBuilder / ibQueryRenderer — a query IR  -> SELECT / DML / DDL text
//   L2-2  THIS FILE                        — a materialize spec -> trigger / view text
//
// Both are pure renderers driven by a driver's dictionary, and neither knows anything about
// metadata. That is why this lives here, beside ibMaterializationDialect, rather than up in
// query/: a renderer must not sit a floor above the dictionary it reads. L3-2 (the schema door)
// does not render — it DECLARES what it wants and APPLIES what comes back.
//
// Which is also why the spec below speaks in plain names and SQL fragments, never in
// ibBackendQueryColumn: a logical column expanding into its physical fields is a metadata
// question, and it is answered before we get here. Keeping L2-2 metadata-blind is what keeps the
// dependency pointing downward — and, incidentally, what makes the whole trigger shape testable
// by reading a string.
//
// WHY NOT FOLD IT INTO L2-1. A trigger body is imperative and diverges between engines in
// STRUCTURE, not spelling: Firebird must MERGE where PostgreSQL and SQLite ON CONFLICT, and T-SQL
// fires once per statement over pseudo-tables instead of once per row. Expressing that in the
// query IR would mean either imperative nodes (RETURN / IF / NEW / OLD) inside it, or a raw-SQL
// escape hatch — and the hatch would dissolve L2-1's "never raw SQL" invariant for every other
// caller. A second renderer with a second dictionary keeps both halves honest.
//
// PLACEHOLDER CONVENTION. Expressions are written over the movement row with one placeholder,
// {row}, replaced per event by the dialect's NEW / OLD alias. That is what lets ONE declaration
// serve all three triggers: insert applies +value over NEW, delete applies the NEGATED value over
// OLD, update does both.
//
// (docs/register-totals-strategy.md §4)

#include "backend.h"
#include "databaseLayer.h"          // ibMaterializationDialect / ibDialectDictionary / ibTotalsPeriod
#include "databaseQueryBuilder.h"   // ibQueryRelPtr — the READ half hands back an L2-1 relation

#include <vector>

// One accumulating column: which column of the derived table, and what a single movement adds to
// it. For a signed register the two sides are two deltas whose value expressions are
// complementary CASEs over the record type — so the accumulate stays unconditional and only its
// VALUE branches.
struct ibMaterializeDelta
{
	wxString m_column;      // physical column of the derived table
	wxString m_valueExpr;   // SQL over {row} — what this movement contributes
};

// How a view column is computed from the stored ones. These are PRIMITIVES, not meanings: L2-2
// knows how to spell them for an engine and nothing about what they are used for. A register's
// "opening balance" is a running sum excluding the current row — that naming lives in metadata,
// which composes these; putting it here would move business vocabulary into the one layer that
// must stay dialect-only.
enum class ibMaterializeAgg
{
	Value,                      // the column as stored (shard-folded when split)
	Difference,                 // A - B
	RunningSum,                 // SUM(<expr>) OVER (PARTITION BY keys ORDER BY period)
	RunningSumExcludingCurrent  // the same running sum, minus this row's own contribution
};

struct ibMaterializeViewColumn
{
	wxString         m_alias;
	wxString         m_columnA;                                 // the primary stored column
	wxString         m_columnB;                                 // the second operand (Difference and the running forms)
	ibMaterializeAgg m_agg = ibMaterializeAgg::Value;
};

// One view over the derived table.
//
// The view is the BRIDGE. Above it: an ordinary relation — it joins, filters, pages and
// RLS-restricts through the engine's own machinery, indistinguishable from a table, with no special
// case anywhere upstream. Below it: the materialisation — a trigger-maintained table, possibly
// split across shards. Swapping what sits underneath changes a view body and nothing else, and
// that is precisely what lets the same queryables run over live aggregation today and over
// maintained totals later.
struct ibMaterializeView
{
	wxString m_name;
	bool     m_withPeriod   = true;    // project the period + every coarser unit as its own column
	bool     m_dropZeroRows = false;   // HAVING <any column> <> 0 — "no value means no row"
	std::vector<ibMaterializeViewColumn> m_columns;

	// ⭐⭐ THE SECOND ARM — the rows the stored table does not carry YET.
	//
	// A maintained total is complete only up to the grain it is stored at: everything that happened
	// inside the current period is in the MOVEMENTS and nowhere else. So the view can offer both
	// halves as one relation — the stored rows UNION ALL the movements, each movement contributing
	// exactly what it contributed to the total (the delta expressions, unchanged).
	//
	// The renderer does not know what that buys. It is a union of two row sources with one column
	// list, and the caller decides where to cut between them: a reader takes stored rows below some
	// boundary and movement rows above it, and neither half is counted twice. Ask for a boundary at
	// or above the stored grain and the movement arm matches nothing — it costs a scan the planner
	// prunes, which is why this can be the ONE relation both kinds of question read.
	bool m_withMovements = false;

	// Columns that exist only on the movement arm — the recorder's fields, a line number. NULL on
	// the stored arm, which is also what tells the two apart without a flag column of its own.
	std::vector<wxString> m_movementColumns;
};

// Everything L2-2 needs to render the maintenance bundle. Pure names and SQL fragments: the
// caller (L3-2) has already expanded logical columns into physical fields.
struct ibMaterializeSpec
{
	wxString m_table;    // the derived table being maintained
	wxString m_source;   // the table whose writes drive it — the triggers hang here

	// The read views to build over m_table. L2-2 does not know what any of them MEAN — it knows how
	// to spell a shard fold, a difference and a running sum for this engine, and the caller composes
	// those into whatever it needs. That separation is the point: a register's virtual tables are a
	// metadata concept, and teaching the SQL layer about "balances" would put business vocabulary in
	// the one place that must stay dialect-only.
	std::vector<ibMaterializeView> m_views;

	// The derived table's KEY — the columns the delta upserts against. Physical field names, in
	// order; the shard column (when split) is added by the renderer, not by the caller.
	std::vector<wxString> m_keyColumns;

	// The period key: which key column holds the truncated period, and the movement expression it
	// is truncated FROM. Empty column = this table has no period dimension.
	wxString       m_periodColumn;
	wxString       m_periodSourceExpr;                  // SQL over {row}
	ibTotalsPeriod m_periodUnit = ibTotalsPeriod::Month; // the STORED grain — the floor on what can be read back

	std::vector<ibMaterializeDelta> m_deltas;

	// Optional guard over {row}: apply this delta only when the movement populates this side.
	// Empty = unconditional.
	wxString m_guard;

	// Split totals: how many shard rows one logical key spreads across. 1 = not split.
	unsigned int m_shards = 1;
};

// One rendered statement. A distinct TYPE rather than a bare wxString, so the schema door can
// accept "something L2-2 produced" and nothing else — a plain string would make the applier look
// like a general-purpose SQL channel, and the rule that only this renderer may feed it would rest
// on a comment. Only RenderMaterialization can mint one (the ctor is private to it), which turns
// the rule into something the compiler holds.
class BACKEND_API ibRenderedStatement
{
public:
	bool IsEmpty() const { return m_sql.IsEmpty(); }

private:
	explicit ibRenderedStatement(wxString sql, wxString guard = wxString())
		: m_sql(std::move(sql)), m_guard(std::move(guard)) {}
	const wxString& Sql() const { return m_sql; }

	wxString m_sql;

	// A statement may carry its own PRECONDITION: a query that must return a row before it runs.
	// Used by drops on engines without IF EXISTS, where attempting to remove a missing object is
	// not a harmless no-op — Firebird rolls the transaction back, so the first apply would destroy
	// itself. Empty = run unconditionally.
	wxString m_guard;

	friend struct ibMaterializeSql;   // MINTS and RUNS them — both stay inside this level
};

// The rendered bundle, in APPLY ORDER: run m_drop first (replacing a bundle whole), then m_create.
struct BACKEND_API ibMaterializeSql
{
	std::vector<ibRenderedStatement> m_drop;     // view(s) + the three triggers (+ functions where separate)
	std::vector<ibRenderedStatement> m_create;   // the three triggers (+ functions) + the views

	bool IsEmpty() const { return m_drop.empty() && m_create.empty(); }

	// Minting point — used by the renderer only; nothing else can construct a statement.
	// A drop may carry a guard: the probe that decides whether the object is there to remove.
	void AddDrop(wxString sql, wxString guard = wxString())
	{
		m_drop.push_back(ibRenderedStatement(std::move(sql), std::move(guard)));
	}
	void AddCreate(wxString sql) { m_create.push_back(ibRenderedStatement(std::move(sql))); }

	// APPLY the bundle to a connection. Execution lives HERE, at the level that produced the text,
	// exactly as ibDatabaseQueryBuilder runs the queries it builds — no SQL string ever leaves L2-2,
	// and no floor above handles one.
	//
	// The DROPs are best-effort: on a first create there is nothing to drop, and Firebird has no
	// IF EXISTS for a view or a trigger. Such a failure must also CLEAR the driver's error state,
	// or a routine "it wasn't there" resurfaces later in the apply's error chain and reads as a
	// failed metadata update.
	//
	// Returns false if a CREATE failed (the caller aborts the apply); drop failures never fail.
	bool Apply(ibDatabaseLayer& conn) const;

	// Is everything this bundle would CREATE already in the database? Asked through the same
	// existence probes the guarded drops use — a read, never DDL. Lets an apply leave an untouched
	// register alone while still HEALING one whose objects went missing (a half-failed earlier
	// apply, a hand-dropped view). A bundle that creates nothing is trivially installed.
	bool IsInstalled(ibDatabaseLayer& conn) const;

	// INSPECTION for tests and diagnostics — reading the text, never a way to run it: Apply above
	// takes the bundle, not a string, so text obtained here cannot be fed back in.
	wxString CreateText() const { return Join(m_create); }
	wxString DropText()   const { return Join(m_drop); }
	wxString LastCreate() const { return m_create.empty() ? wxString() : m_create.back().m_sql; }
	wxString CreateAt(size_t i) const { return i < m_create.size() ? m_create[i].m_sql : wxString(); }

private:
	static wxString Join(const std::vector<ibRenderedStatement>& v)
	{
		wxString out;
		for (const ibRenderedStatement& s : v) out += s.m_sql + wxT("\n");
		return out;
	}
};

// ==========================================================================
// READING the materialised surface — the other half of L2-2.
//
// Building the maintenance is only one direction; the other is querying what it maintains, and
// that query is just as engine-shaped: conditional sums over a period range, a non-zero filter,
// a grouped fold. Leaving it to the metaobject meant assembling L2-1 expressions by hand up in
// metadata — the exact thing this level exists to absorb.
//
// So a reader DESCRIBES what it wants in the same vocabulary the views are built from, and gets
// back a relation ready to drop into a FROM. Metadata names the columns; L2-2 spells the SQL.
// ==========================================================================

// When a column's contribution counts. This is what turns one grouped pass over the turnovers
// surface into an opening balance, the movements of an interval, and a closing balance at once —
// see the balance-and-turnovers reading, which needs all three from a single scan.
enum class ibMaterializeWhen
{
	Always,       // every row in range
	BeforeFrom,   // period <  from   — what was carried INTO the interval
	InRange,      // from <= period <= to
	UpToTo        // period <= to     — everything through the end of the interval
};

struct ibMaterializeReadColumn
{
	wxString          m_alias;
	wxString          m_columnA;
	wxString          m_columnB;                             // second operand for Difference
	ibMaterializeAgg  m_agg  = ibMaterializeAgg::Value;
	ibMaterializeWhen m_when = ibMaterializeWhen::Always;
	bool              m_aggregate = false;                   // wrap in SUM and GROUP BY the keys
};

struct ibMaterializeReadSpec
{
	wxString m_view;                       // the relation to read — a view, or the totals table
	wxString m_periodColumn;               // empty = this surface carries no period

	// The period bounds, already resolved to values. Invalid = unbounded on that side; the
	// conditional columns above are what give each bound its meaning.
	ibValue  m_from;
	ibValue  m_to;

	std::vector<wxString> m_keyColumns;    // projected, and grouped by when any column aggregates
	std::vector<std::pair<wxString, ibValue>> m_filters;   // column = value, applied INSIDE

	std::vector<ibMaterializeReadColumn> m_columns;
	bool m_dropZeroRows = false;           // keep only rows where some reported figure is non-zero

	// --- where to cut between a union view's two arms -------------------------------------------
	// A view may carry the movements alongside the stored rows (ibMaterializeView::m_withMovements).
	// Both halves describe the same figures, so a read that took them all would count the current
	// grain TWICE. These say where the cut goes, and the caller has resolved every value already.

	// The column that is NULL on the stored arm — empty means this view has one arm and no cut.
	wxString m_markColumn;

	// Where the stored arm ENDS: the start of the grain holding the upper boundary. INVALID is not
	// "no boundary" — it means the question does not reach below the stored grain, so the movement
	// arm is excluded outright, which is also the cheapest way to say "sleep".
	ibValue m_floor;

	// Where the stored arm BEGINS: the first WHOLE grain at or after the lower boundary. An interval
	// that starts at noon cannot take today's stored row — it holds the morning too — so the head of
	// such an interval comes from the movements exactly as its tail does. Invalid = the read is
	// open-ended below (a balance), and the stored arm starts wherever the data does.
	ibValue m_headSplit;

	// The lower boundary's tail past the period, when it names a document. Same shape and order as
	// m_boundaryTail, compared the other way round.
	std::vector<std::pair<wxString, ibQueryExprPtr>> m_boundaryHead;

	// Whether each boundary's own position is OUTSIDE the interval — "everything BEFORE this
	// document" rather than "up to and including it". It changes one operator per side, and getting
	// it wrong is wrong by exactly one document, which is the kind of wrong nobody notices.
	bool m_toExcluding   = false;
	bool m_fromExcluding = false;

	// The boundary's tail past the period, compared in order: the recorder's fields, when the
	// boundary names a document rather than an instant. Three documents can share a date, and this
	// is what tells them apart — in the same order the value type compares them by.
	//
	// EXPRESSIONS rather than values, because a reference's key is bound as opaque BYTES: the caller
	// decomposes the boundary through the same write codec the rows were stored with, and hands over
	// what came out. L2-2 compares a column against a node and asks nothing about what is inside it.
	std::vector<std::pair<wxString, ibQueryExprPtr>> m_boundaryTail;
};

// Build the relation for such a read. Returns a subquery relation — the caller drops it straight
// into a FROM, so everything around it (JOIN, outer WHERE, paging, RLS) is ordinary SQL the engine
// handles. Parameters ride INSIDE it, which is what keeps the selection on the server rather than
// filtering an already-materialised result.
BACKEND_API ibQueryRelPtr RenderMaterializedRead(const ibMaterializeReadSpec& spec,
                                                 const wxString& alias);

// ==========================================================================
// The ONE entry point a floor above uses: hand over a connection and a declaration.
//
// Which dictionaries exist, whether this driver can maintain derived state, how the statements are
// spelled and in what order they run — all of that is L2-2's business and stays here. The caller
// knows only that it wants this table maintained on that connection.
// ==========================================================================

// Can this driver maintain derived state at all? False for a driver with no materialization
// dictionary (ODBC — it cannot know the engine underneath, so it cannot template a trigger).
// Callers use it to decide whether a materialised read surface exists to query.
BACKEND_API bool ibCanMaterialize(const ibDatabaseLayer& conn);

// What an apply actually did — so the caller reports a rebuild only when one happened. An apply
// that touches one register used to announce a rebuild for EVERY register, which is worse than
// noise: it hides the one line that matters in a list of lines that do not.
enum class ibMaterializeApply { Unchanged, Rebuilt, Failed };

// Render the declaration for this connection's engine and apply it — but only when it DIFFERS from
// what is already installed. `previous` is the baseline declaration (null for a new table); the
// comparison is over the rendered text, because the bundle IS that text: identical text means the
// drop-and-recreate would put back exactly what is there.
//
// The text alone is not enough, though — it says what SHOULD be installed, not what IS. So an
// unchanged declaration is also PROBED, and a bundle whose objects went missing is rebuilt anyway.
//
// A driver that cannot materialise is Unchanged: its registers simply read from live aggregation.
BACKEND_API ibMaterializeApply ibApplyMaterialization(ibDatabaseLayer& conn,
                                                      const ibMaterializeSpec& spec,
                                                      const ibMaterializeSpec* previous = nullptr);

// REMOVE the maintenance and nothing else — the derived table itself is the caller's business.
//
// Needed because a bundle outlives the declaration that produced it. When a table is dropped (a
// register deleted, or one whose kind changed so its totals live in a differently-named table),
// the triggers do NOT go with it: they hang on the MOVEMENTS table and merely mention the totals
// by name. Left behind, they fire on the next write into a table that no longer exists — so the
// movements become unwritable, and the failure surfaces far from the change that caused it.
//
// The spec passed here is the OLD one: it names the objects as they were created.
BACKEND_API bool ibDropMaterialization(ibDatabaseLayer& conn, const ibMaterializeSpec& spec);

// Render the bundle. `mat` may be null (the driver cannot maintain derived state — ODBC), in
// which case the result is EMPTY and the caller simply builds a plain table: the register keeps
// serving its readings from live aggregation, correct at any scale and only slower. Emptiness is
// a supported outcome, not an error.
//
// `dialect` supplies the two facts borrowed from the query side — the period truncation map (so a
// trigger's key and a view's projection are the SAME expression by construction) and the
// source-less-SELECT dummy relation used by guarded deltas.
//
// Throws when the spec asks for a period unit the engine has no form for. Refusing loudly is
// deliberate: the alternative is a silently wrong grouping key that reconciles to nothing.
BACKEND_API ibMaterializeSql RenderMaterialization(const ibMaterializeSpec& spec,
                                                   const ibMaterializationDialect* mat,
                                                   const ibDialectDictionary& dialect);

// The effective shard count: the request, collapsed to 1 when the engine has no connection-id to
// hash (SQLite — a single-writer engine has no contention to relieve, so a split would be pure
// read tax). Exposed because the STRUCTURE side needs the same answer to decide whether the
// derived table carries a shard column at all.
BACKEND_API unsigned int EffectiveShardCount(const ibMaterializeSpec& spec,
                                             const ibMaterializationDialect* mat);

// Name of the shard column on a split table. Fixed, not configurable: it is an implementation
// detail the view absorbs, and nothing above L3 ever names it.
BACKEND_API const wxChar* ShardColumnName();

#endif // !__DATABASE_MATERIALIZE_BUILDER_H__
