#ifndef __DATA_QUERY_BUILDER_H__
#define __DATA_QUERY_BUILDER_H__

// ibDataQueryBuilder — the universal (half-)L3 read entry. ONE door for every
// metadata read (catalog/document lists, hierarchical trees, enums today;
// registers / reports later) — NOT list-specific. Parallel to L2's
// ibDatabaseQueryBuilder: L2 is physical, L3 (this) is metadata.
//
// L3 in one line: built from a HOLDER (to execute) + METADATA (to resolve), it
// generates L2 BY SUBSTITUTING NAMES — metadata names become physical names:
//     metaobject         -> physical table          (GetPhysicalTableName)
//     attribute (metaID)  -> physical SQL field(s)    (_N / _S / _R)
//     reference / parent  -> guidName / _RRRef blob
// A metadata predicate "attribute X = value" becomes the L2 IR
// "physical_field_X = Const". That name substitution IS L3's job; the resulting
// ibQueryIR is metadata-free, so L2 stays metadata-blind. The substitution
// primitives live in ibMetaIRBuilder.
//
// Lives in backend/query/ — the dedicated L3 home (the L4 text parser + the L4-2
// LINQ fold both lower into this entry; the runtime invokes it).
//
// PERFORMANCE (docs/query-language-arc.md §19) — under every scroll tick:
//   * cacheable, no connection: the metadata resolution (the model/buffer holds
//     an ibDataQueryBuilder across ticks; rebuilds only on filter/sort change);
//   * per-fetch, borrow→run→release: FetchPage builds the page IR (cheap), runs
//     via L2, returns the RAII cursor whose dtor releases the holder reservation
//     (§12 leak protection). No connection pinned across ticks (keyset paging);
//   * the cursor anchor rides as a bound PARAM, so the SQL text is byte-identical
//     across ticks in one direction (driver prepared-statement cache hits).
//
// Row materialisation is fully L3: every consumer (list / tree / enum / register)
// reads through the selection's GetValue() (the row guid via the uuid identity column);
// no caller touches the L2 result set. HEADER PURITY: this header names NO L2 type. The L2 cursor
// is pimpl'd inside ibDataQueryResult, the rendered-page cache is opaque (built
// through a factory), the IR-building primitives (the former ibMetaIRBuilder)
// live entirely in the .cpp, and the two private members that traffic in L2 IR
// only forward-declare it. So L3 callers include this without dragging in L2.

#include "queryable.h"                                     // ibBackendQueryable, ibQueryCondition, ibQuerySortItem (L2-free)

// WHICH of the two access questions is being asked — see ibAccessPolicy. Table is the right to
// touch the source at all (before the statement); Value judges what the statement did.
enum class ibAccessStage { Table, Value };


#include "queryRamTable.h"                                  // ibQueryRamTable — the raw flat snapshot Execute() returns (L2-free)
#include "querySelectorTree.h"                              // ibSelectorTree — the folded node tree SelectTotals returns (L2-free)

#include <vector>
#include <utility>
#include <memory>
#include <functional>   // the detach callback — see ibDataQueryResult::DetachColumns

class ibValueMetaObjectAttributeBase;
class ibDatabaseConnectionHolder;
class ibTempTableManager;   // RLS temp-promote — a computed inner (value table / multi-source register) materialised to a DB temp table (non-FB), owned WITH the builder
class ibQueryResult;        // L2 cursor — consumed by the DB result source
class ibDataResultSource;   // the selection's backing (DB scan OR RAM table) — pimpl, defined in the .cpp
class ibSelector;           // the traversal a result hands out: result.Select(mode) — defined in querySelector.h
struct ibRenderedPageCache; // build-once page cache — opaque; defined in the .cpp, created via NewPageCache()
struct ibDataQuerySpec;     // the door's accumulated query as a value — defined below the class, read by the provider

// === selection KINDS (two axes) + aggregate / totals PODs ====================================
// At NAMESPACE scope (before ibDataQueryResult, which names them). ibDataQueryBuilder re-exposes the
// aggregate PODs as nested aliases for back-compat (ibDataQueryBuilder::AggregateItem / AggregateFn).
//
// AXIS 1 — TRAVERSAL kind (Select(kind)): Direct (rows as-is) / ByGroups (by levels) /
//   ByGroupsHierarchy (by levels + dimension hierarchy).
// Plain enum (implicitly numeric) with prefixed enumerators — the system-enum convention, so it can be
// reflected to the runtime like the other 30+ enums (ibValueEnumeration needs the enum→number conversion).
enum ibSelectKind { ibSelectKind_Direct, ibSelectKind_ByGroups, ibSelectKind_ByGroupsHierarchy };
// AXIS 2 — DIMENSION kind (TotalBy field): Elements / Hierarchy / HierarchyOnly — how a reference
//   field unfolds (folders).
enum class ibDimensionKind { Elements, Hierarchy, HierarchyOnly };

enum class ibAggregateFn { Sum, Count, Min, Max, Avg };

// One row's worth of write assignments — the column and the value to put in it. A write is a
// VECTOR of these; a single-row write is a vector of one, which is why nothing had to learn a
// second shape when batching landed.
using ibWriteRow = std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>;
// One output aggregate (.Sum/.Count…): rolls IN-PLACE into m_col (alias vestigial, used only by the
// flat SelectAggregate path). m_col null = COUNT(*).
// m_path (non-empty) = the aggregate input is a reference DOT-WALK leaf (SUM(Producer.Weight)) — the
// provider joins the path and qualifies m_col (== m_path.back()) by the join alias. Empty = plain column.
// m_expr (set) = the aggregate input is a COMPUTED expression (SUM(Qty * Price)) — the provider lowers
// it via BuildColumnExpr; m_col stays null, m_path empty. Single-source DB aggregates only (gated).
// m_distinct = fold over DISTINCT values of the input — `COUNT(DISTINCT Board)`: how many different
// boards, not how many rows have one. A property of THIS aggregate, not of the SELECT (the
// statement's own DISTINCT is a different question about the whole row).
struct ibAggregateItem { ibAggregateFn m_fn; const ibBackendQueryColumn* m_col; wxString m_alias;
                         std::vector<const ibBackendQueryColumn*> m_path;
                         ibQueryColumnExprPtr m_expr;
                         bool m_distinct = false; };
// ONE FIELD of a TotalBy level — the column to roll up by + how it unfolds.
struct ibTotalField    { const ibBackendQueryColumn* m_col; ibDimensionKind m_dim; };

// One TotalBy dimension level. Levels apply IN ORDER; a level holds ONE OR MORE fields and its
// group key is the TUPLE of their values — "by partner AND contract" is one level, not two nested
// ones, and a field that repeats what the key already says simply adds no rows.
struct ibTotalLevel
{
	std::vector<ibTotalField> m_fields;

	// THE HEAD — the first field. The gates that only make sense over ONE field (the hierarchy
	// unfold, the server-side group-level page) ask through here and check IsSingleField first.
	const ibBackendQueryColumn* HeadCol() const { return m_fields.empty() ? nullptr : m_fields.front().m_col; }
	ibDimensionKind             HeadDim() const { return m_fields.empty() ? ibDimensionKind::Elements : m_fields.front().m_dim; }
	bool IsSingleField() const { return m_fields.size() == 1; }

	// One field, spelled the way every existing caller states a level.
	static ibTotalLevel One(const ibBackendQueryColumn* col, ibDimensionKind dim) {
		ibTotalLevel level; level.m_fields.push_back(ibTotalField{ col, dim }); return level;
	}
};

// A dot-walk select column — a reference-navigation PATH of query columns + the
// output alias the leaf is projected under. The path's non-leaf segments are single-
// target reference columns (each resolved to its target queryable and joined on the
// self-reference key); the leaf is read on the final target. Trafficking in the L3
// column abstraction (ibBackendQueryColumn), not the concrete metaclass. (docs §22)
struct ibDotWalkColumn
{
	std::vector<const ibBackendQueryColumn*> m_path;    // ref segments + leaf column
	wxString                                 m_alias;   // output name (read via GetColumn)
};

// One page request — universal across read modes. The caller fills the fields
// its mode needs; the rest stay defaulted.
struct ibReadPageRequest
{
	ibFetchDirection     m_direction = ibFetchDirection::Forward;
	bool                 m_hasAnchor = false;       // cursor present? (first page = false)
	std::vector<ibValue> m_anchorSortValues;        // anchor value per sort column, in order (the keyset cursor —
	                                                // the row PK rides here as a reference value, its own tail)
	int                  m_count = 0;               // batch size (caller adds its own +1 probe)
	bool                 m_reverseSort = false;     // backward-walk flips ASC/DESC
	bool                 m_lockForUpdate = false;   // pessimistic read-for-update (register set lock) — adds the dialect's row-lock clause

	// --- hierarchy (tree) — optional -------------------------------------
	// Set ONLY on a hierarchy (tree) drill — a scope filter that fetches the children of
	// the browsed parent node. No non-hierarchy read touches these.
	bool      m_hierarchyFilter = false;   // filter by the parent reference (tree mode)
	const ibBackendQueryColumn* m_hierarchyCol = nullptr;   // the parent-ref attribute AS a column (provider derives the physical field)
	ibValue   m_hierarchyKey;              // browsed parent node KEY — the parent reference VALUE (empty = top level).
	                                       // A value, NOT a bare guid: the provider encodes it like any keyset key
	                                       // (a reference self-describes its metaID; a non-reference hierarchy rides inline)
	bool      m_isTopLevel  = false;       // top-level rows (empty parent)
	bool      m_flatScan    = false;       // flat-list view of a tree: skip the hierarchy filter
};

// ibDataQueryResult — the L3 selection. Wraps the L2 cursor and yields READY
// ibValue rows (reference reconstruction, composite assembly) — consumers never
// touch the raw driver result set. The L2 ibQueryResult is held through a
// unique_ptr (pimpl) so this header forward-declares it rather than including
// L2; the cursor's dtor (run when this selection dies) releases the holder
// reservation. Move-only — the special members are out-of-line (defined where
// ibQueryResult is complete).
class BACKEND_API ibDataQueryResult
{
public:
	// DB backing — wraps the L2 cursor (physical scan).
	ibDataQueryResult(ibQueryResult&& cursor, const ibBackendQueryable* queryable);
	// RAM backing — every RAM source rides on ONE table type, ibQueryRamTable (L3's own;
	// names no runtime type): the multi-source JOIN / UNION / aggregate result AND a
	// computed virtual table (slice / balance / turnover / temp, via ComputeRows). The
	// selection walks it with the same Next() / GetValue() surface, so consumers never
	// learn which backing they have. (docs §22.1a, §22.4d, §22.6)
	ibDataQueryResult(ibQueryRamTable&& ramTable, const ibBackendQueryable* queryable);
	~ibDataQueryResult();
	ibDataQueryResult(ibDataQueryResult&&) noexcept;             // moves the unique_ptr backing — noexcept
	ibDataQueryResult& operator=(ibDataQueryResult&&) noexcept;
	ibDataQueryResult(const ibDataQueryResult&)            = delete;
	ibDataQueryResult& operator=(const ibDataQueryResult&) = delete;

	bool Next();                                   // advance the selection

	// Typed value of a COLUMN in the current row, as a ready ibValue. The backing
	// (DB cursor / computed RAM table) recovers the attribute behind the column on its
	// own (no downcast) and materialises — so the reference column yields the row's
	// reference object too, and callers loop ALL columns uniformly. A RAW column (the
	// uuid; a computed balance field) is read straight off the backing by its RawType.
	// The row guid is NO special case: read the uuid identity column —
	//   GetValue(queryable->GetPrimaryKeyColumns().front())
	// (the raw overload takes a column by ref so a temporary works). No GetGuidString.
	ibValue GetValue(const ibBackendQueryColumn* col) const;
	ibValue GetValue(const ibRawDBColumn& rawColumn) const;

	// A result column by its OUTPUT NAME — for aggregate columns (SUM/COUNT/… AS
	// alias) that have no source column. Group columns still read via GetValue(col).
	ibValue GetColumn(const wxString& alias) const;

	// A dot-walk leaf that is a reference / enum / composite — reassembled from its field spread projected
	// under `prefix` (vs GetColumn, which reads one scalar field). An empty / broken ref reads as the empty value.
	ibValue GetColumnObject(const wxString& prefix, const ibBackendQueryColumn* col) const;

	// TRAVERSAL — selection = result.Select(mode). Drains the cursor ONCE into a flat snapshot
	// (the materialise-columns the door stamped) and hands it to an ibSelector, which decides HOW to
	// walk it per the mode — flat / hierarchy / hierarchy-only — polymorphically; the caller sees one
	// interface, never which. Configure the fold on the returned Selector (ByParentRef / ByGroups /
	// Aggregating). Consumes the result (the cursor is drained). Defined where ibSelector is complete.
	// (docs/query-language-arc.md §22.1b)
	ibSelector Select(ibSelectKind kind = ibSelectKind::ibSelectKind_Direct);

	// The door stamps the columns a later Select(mode) must materialise (its Select() output list +
	// aggregate inputs) — so the snapshot carries exactly what the fold reads. No-op for the working
	// fetch path (it consumes Next()/GetValue() directly and never calls Select(mode)).
	void SetMaterialiseColumns(std::vector<const ibBackendQueryColumn*> cols);

	// The door also stamps the totals config — the TotalBy dimension levels (in order) + the common
	// totals aggregate set — so result.Select(kind) folds by them automatically (no manual fold on
	// the Selector). (docs/query-language-arc.md §22.1b)
	void SetTotals(std::vector<ibTotalLevel> levels, std::vector<ibAggregateItem> aggregates,
		bool overall = false);

	// ⭐ THE TOTALS TREE, ALREADY FOLDED BY THE DBMS. Stamped when the read was pushed down as
	// `GROUP BY ROLLUP`: the result then has no detail cursor to walk, and Select(kind) hands this
	// tree over instead of building one. Same levels, same aggregates, same shape — the push-down is
	// not taken where they would differ.
	void SetReadyTree(class ibSelectorTree tree);

	// A dot-walked dimension whose leaf is NOT one field: say under which alias PREFIX its spread was
	// projected and which column reassembles from it. See m_objectReads.
	void SetObjectRead(ibMetaID dimColumnId, const wxString& prefix, const ibBackendQueryColumn* leaf);

	// The stamped totals config — read by result.Select(kind) to configure the Selector's fold.
	const std::vector<ibTotalLevel>&    TotalLevels()     const { return m_totalLevels; }
	const std::vector<ibAggregateItem>& TotalAggregates() const { return m_totalAggregates; }
	bool                                TotalsOverall()   const { return m_totalsOverall; }

	// The door also stamps the SOURCE (holder + queryable + select list + filter) so the Selector can
	// run LAZY sub-selections (selection.Select() re-Executes a node's children) and resolve a
	// reference-dimension's catalog hierarchy. (docs/query-language-arc.md §22.1b)
	// ⭐⭐ THE SOURCE COMES WITH ITS OWNERSHIP — `owned` is the skeleton, not a second bookkeeping list.
	//
	// A subquery / UNION branch is wrapped in an ibSubqueryQueryable that owns the columns it
	// publishes, and a result points at those columns by raw pointer from four lists (materialise
	// columns, totals levels, totals aggregates, this recipe) which are read long after the lowering
	// returned. Releasing the wrappers at the end of the run therefore left every one of those
	// pointers dangling — 2026-08-19: Select() read GetTypeDesc() through freed memory while a report
	// was being composed.
	//
	// So the sources travel WITH the recipe, as shares: the builder is one owner, the result becomes
	// another (Max: "the two of them own it — the query dies, the result still holds the skeleton…
	// however many selects there are, they all own it, and it goes when the last of them goes").
	// Nothing is copied, so a column stays the SAME column: same identity, same id, same type object.
	void SetSource(ibDatabaseConnectionHolder* holder, const ibBackendQueryable* queryable,
		std::vector<std::pair<const ibBackendQueryColumn*, wxString>> selectCols,
		std::vector<ibQueryCondition> conditions,
		std::vector<std::shared_ptr<const ibBackendQueryable>> owned = {});

private:
	// The backing — a physical DB scan OR a computed RAM table, behind one
	// interface. The selection NEVER branches on which: it forwards to the source.
	// DB and RAM stay fully separate (two source classes in the .cpp), and no
	// consumer — not even this class — learns the backing. (docs §22.4d)
	std::unique_ptr<ibDataResultSource> m_source;
	std::vector<const ibBackendQueryColumn*> m_matColumns;   // columns a Select(mode) drains into the snapshot
	// Co-ownership of the sources built for the query (AdoptSources) — every column pointer above
	// lives inside one of them, so they stay valid for exactly as long as this result does.
	std::vector<std::shared_ptr<const ibBackendQueryable>> m_ownedSources;

	// ⭐ HOW A DOT-WALKED DIMENSION IS READ. Its value is not one projected field: a reference /
	// enum / composite leaf is projected as its whole physical SPREAD under an alias PREFIX, and it
	// reassembles from those fields (ColumnObject). The pair is stamped by the builder — the door
	// knows it, the snapshot does not — keyed by the dimension column's own id.
	struct ibObjectRead { wxString m_prefix; const ibBackendQueryColumn* m_leaf = nullptr; };
	std::map<ibMetaID, ibObjectRead> m_objectReads;
	std::vector<ibTotalLevel>    m_totalLevels;              // totals dimensions (stamped by the door)
	std::vector<ibAggregateItem> m_totalAggregates;          // common totals aggregate set
	bool                         m_totalsOverall = false;    // walk the tree's ROOT as a row (BY OVERALL)
	// Set when the DBMS folded the levels itself — see SetReadyTree.
	std::shared_ptr<class ibSelectorTree> m_readyTree;
	// source recipe (for lazy sub-selections + reference-dimension resolution)
	ibDatabaseConnectionHolder*  m_srcHolder    = nullptr;
	const ibBackendQueryable*    m_srcQueryable = nullptr;
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> m_srcSelectCols;
	std::vector<ibQueryCondition> m_srcConditions;
};

// ibRenderedPageCache — build-once cache for a repeated page query (one scroll
// shape across ticks). Held by the persistent list model (via shared_ptr);
// Select() reuses the resolved identity sort + the rendered SQL / bind plan while
// only the external anchor Param vector changes per tick. Self-invalidating on a
// signature mismatch. It stores an L2 ibRenderedQuery, so its DEFINITION lives in
// the .cpp and it is OPAQUE here — the list model holds it through a shared_ptr
// and creates it via ibDataQueryBuilder::NewPageCache(), never naming its layout.

// L3 join kind — the door's own (NOT L2's ibQueryJoinType; the door is L2-blind).
enum class ibQueryJoinKind { Inner, Left, Right, Full };   // Right / Full -> RAM stitch only (co-located does Inner/Left)

// Join ON comparison operator. Independent of the L4 AST's ibQueryCompareOp (L3 must not depend on the L4
// parser). Eq is the hash-join fast path; a non-equi op is a theta join. A COLUMN-KEYED theta co-locates —
// BuildColocatedFrom emits the node's real operator server-side (JoinOpToBinOp, dbTableProvider.cpp) — so only
// a COMPUTED ON forces the RAM nested-loop stitch. (Landed 2026-07-16; the gate is `allColumnKeyed`.)
enum class ibJoinCompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// A JOIN's ON condition — grouped (was six scattered fields on ibQueryNode) so the mutually-exclusive ways to
// express it live in one cohesive place. By convention: m_cross = cartesian (ON TRUE); else m_exprL/m_exprR
// set = a COMPUTED theta join (a.x+1 <op> b.y), always RAM nested-loop — no column key to qualify against a
// leaf's table; else m_colL/m_colR = explicit key columns (Eq -> hash, other m_op -> theta), co-locatable
// either way.
//
// ⚠ THERE IS NO FOURTH CASE ANY MORE. "All null and not cross" used to mean an auto-join by
// reference, worked out from the metadata — which made one query text mean different things as the
// metadata changed. Nothing produces that shape now; an ON is written or the join is a product.
struct ibJoinOn {
	const ibBackendQueryColumn*  m_colL  = nullptr;   // explicit key columns
	const ibBackendQueryColumn*  m_colR  = nullptr;
	ibJoinCompareOp              m_op    = ibJoinCompareOp::Eq;   // Eq -> hash; non-Eq -> theta (server-side when co-located)
	ibQueryColumnExprPtr         m_exprL;                         // computed ON: LEFT-side expr (presence -> theta)
	ibQueryColumnExprPtr         m_exprR;                         // computed ON: RIGHT-side expr
	bool                         m_cross = false;                 // CROSS / ON TRUE — keyless cartesian (RAM only)
};

// (ibSelectKind / ibDimensionKind — defined at namespace scope above, before ibDataQueryResult.)

// ==========================================================================
// ibQueryNode — the L3 relational TREE the door builds and the composer executes.
// A query is NOT a single source: it is a tree of heterogeneous sources combined
// horizontally (Join) and vertically (Union), nestably. Each LEAF is a queryable
// (catalog / temp table / register slice / external) that vends its own provider;
// the composer realizes the tree over those per-leaf providers (co-locate into one
// SQL where possible, else materialise + stitch). Union-ready by shape — that is
// the whole point (FROM catalog, FROM temp table, then UNION).
// (docs/query-language-arc.md §22.1)
// ==========================================================================
struct ibQueryNode
{
	enum class Kind { Source, Join, Union };
	Kind m_kind = Kind::Source;

	// --- Source (leaf) ---------------------------------------------------
	const ibBackendQueryable* m_queryable = nullptr;   // the source this leaf reads
	wxString                  m_alias;                 // disambiguator (the column prefix)

	// --- Join (horizontal): m_left ⋈ m_right ----------------------------
	std::shared_ptr<ibQueryNode> m_left;
	std::shared_ptr<ibQueryNode> m_right;
	ibQueryJoinKind              m_joinKind = ibQueryJoinKind::Inner;
	ibJoinOn                     m_on;       // the ON condition: keys / op / computed exprs / cross — see ibJoinOn

	// --- Union (vertical): N branches of one shape ----------------------
	std::vector<std::shared_ptr<ibQueryNode>> m_parts;
	// Parallel to m_parts: true = this branch was attached with UNION ALL (keep duplicates).
	// A plain-UNION branch (false) DEDUPES the accumulated rows at its operator — SQL semantics
	// (left-assoc: A UNION B UNION ALL C dedupes after B, keeps C's duplicates). parts[0] = true.
	std::vector<bool> m_partAll;

	static std::shared_ptr<ibQueryNode> Source(const ibBackendQueryable* q, const wxString& alias = wxEmptyString) {
		auto n = std::make_shared<ibQueryNode>();
		n->m_kind = Kind::Source; n->m_queryable = q; n->m_alias = alias;
		return n;
	}
};

// --- Access policy (RLS) — the L3-side INTERFACE only ----------------------
// The door is metadata- AND runtime-blind: access enforcement is injected as an
// OPAQUE policy. The runtime builds the concrete implementation on the SESSION
// side; the default ctor pulls it via session->GetAccessPolicy() (same place it
// pulls the holder — no new runtime include leaks into L3). The door only CALLS
// it — it never resolves roles / modules / metadata itself. Null = no
// enforcement (migration-safe; trusted / internal read paths pass none).
class BACKEND_API ibAccessPolicy
{
public:
	virtual ~ibAccessPolicy() = default;

	// ONE METHOD PER STAGE. The door calls the one matching what it is about to do and runs the
	// query — it keeps nothing of its own about access: no right, no table/row distinction, no
	// second-guessing of a statement afterwards. How each stage is checked is entirely the
	// policy's business: which right answers for it, whether that right guards the table or the
	// rows, and what restriction gets folded into `query` through the builder's own verbs (a
	// semi-join to the user's allowed set, a Where). A hard deny throws ibBackendAccessException —
	// the extreme of the same mechanism, a restriction that admits nothing.
	// ONE METHOD PER OPERATION, each asked twice — the stage says which of the two questions:
	//
	//   Table — BEFORE the statement: may this role touch these sources at all? Answered from the
	//           rights, plus whatever restriction the policy folds into `query`. This is the
	//           default question, and on a table-controlled source (a register) the whole of it.
	//   Value — AFTER it ran, with what it actually did (`affected` rows): did the write happen?
	//           Only the policy knows whether that number carries meaning — on a row-controlled
	//           source nothing written says the filter kept the statement off a row that is there;
	//           on a table-controlled one an empty result is simply an empty result.
	//
	// FALSE MEANS REFUSED, and a refusal is never silent: the policy either throws it itself, with
	// everything it knows ("not enough access rights: writing to register 'Stock'"), or answers
	// false and the door raises the same exception naming what it was doing. Two stages, two
	// possible refusals: first the right on the source, then — for a write — the right to the row
	// the statement was aimed at. TRUE = carry on.
	//
	// Selecting asks the Table question only: a read has no rows-affected to judge, and rows this
	// role may not see never reach it — the restriction filtered them, nothing to count afterwards.
	virtual bool CheckSelect(class ibDataQueryBuilder& query, const ibAccessStage& stage, long affected = 0) const = 0;
	virtual bool CheckCreate(class ibDataQueryBuilder& query, const ibAccessStage& stage, long affected = 0) const = 0;
	virtual bool CheckUpdate(class ibDataQueryBuilder& query, const ibAccessStage& stage, long affected = 0) const = 0;
	virtual bool CheckDelete(class ibDataQueryBuilder& query, const ibAccessStage& stage, long affected = 0) const = 0;
};

class BACKEND_API ibDataQueryBuilder
{
public:
	// Default — pulls the holder from the CURRENT SESSION. The query shape is
	// set fluently after construction (mirroring L2's ibDatabaseQueryBuilder);
	// nothing is crammed into the constructor.
	ibDataQueryBuilder();
	// Explicit holder — overrides the session default (sessionless subsystems).
	explicit ibDataQueryBuilder(ibDatabaseConnectionHolder* holder);

	// --- fluent shape (chainable) — mirrors L2's verbs, at the metadata level.
	// Fields are identified BY ATTRIBUTE (already resolved; the L4 text parser is
	// what turns a name string into the attribute). NO ibFilterRow/ibSortOrder —
	// those are the dynamic-list (table) layer's types; that layer translates its
	// filter/sort into Where/OrderBy here. (SetParameter / &Param is an L4 thing:
	// at L3 the value goes straight into Where.)
	ibDataQueryBuilder& From(const ibBackendQueryable* queryable, const wxString& alias = wxEmptyString); // primary source -> the relational tree root

	// ⭐ THE SAME DOORS, TAKING THE SOURCE AS AN OWNING HANDLE. A source built FOR this query — a
	// subquery, a UNION branch — dies when the lowering returns, while the RESULT it produced is read
	// afterwards and points into the columns that source publishes. Passing it as a shared_ptr makes
	// the builder a co-owner, and StampResult passes that ownership on to the result: the skeleton
	// then lives until the last holder lets go, with nothing to track and nothing to copy.
	//
	// Metadata-backed sources keep using the raw overloads above — they outlive every query by
	// construction, and wrapping them would say otherwise.
	ibDataQueryBuilder& From(std::shared_ptr<const ibBackendQueryable> queryable, const wxString& alias = wxEmptyString) {
		return From(AdoptOwnedSource(std::move(queryable)), alias);
	}

	// ⭐⭐ A NAMED QUERY THIS STATEMENT DECLARES — `WITH <name> AS (<inner>)`, and then read by NAME.
	//
	// It exists for what a package already says: a statement names its result (`ONTO`) and later
	// statements read it. Until now such a read wrapped the named select in a subquery source, which
	// is a RAM source by construction (ibSubqueryQueryable::IsComputedInRam) — the rows came back to
	// us and the join happened here. Declared instead, the named query stays INSIDE the SQL and the
	// DBMS does the join with nothing in our memory.
	//
	// The inner door is copied whole: it is the same door any query is built with, so nothing about
	// building one changes — only where its text ends up. Declaration ORDER is kept, since an engine
	// reads them top to bottom and one may mention another.
	//
	// ⚠ ONLY WHERE THE ENGINE HAS `WITH` (ibSqlFeatures::m_cte) and only for a DB-backed inner query.
	// The caller decides that — the renderer refuses rather than rewriting, and a RAM source has no
	// SQL to declare in the first place.
	ibDataQueryBuilder& With(const wxString& name, const ibDataQueryBuilder& inner);

	// One declared query: the name it is read by and the door that produces it. PUBLIC because the
	// SPEC carries the list to the provider, which lowers each into the same IR — a private shape
	// there would be a fact the reader cannot name.
	//
	// The inner door is held by shared_ptr so this door stays COPYABLE (a subquery wrapper copies it)
	// and so a copy SHARES the inner query rather than duplicating a whole query per copy.
	struct ibNamedQuery { wxString m_name; std::shared_ptr<ibDataQueryBuilder> m_inner; };

	// --- multi-source composition (the crown) ----------------------------
	// Add another source. Join(b) auto-joins by reference (dot-walk style); Join(b,
	// left, right) joins on explicit columns (temp / RAM / arbitrary). Union(b) stacks
	// b vertically (same projection). The door builds an ibQueryNode TREE; the composer
	// realizes it over each leaf's provider — co-located into one SQL where possible,
	// else materialised + stitched. (docs/query-language-arc.md §22.1)
	// ⛔ NO KEYLESS OVERLOAD. `Join(b)` used to mean "work the link out from a reference" — removed
	// with the derivation behind it. A join carries its ON; a product is CrossJoin, and in the
	// language it is written with a comma.
	ibDataQueryBuilder& Join(const ibBackendQueryable* queryable,
	                         const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString);                            // explicit on-columns (equality)
	ibDataQueryBuilder& Join(const ibBackendQueryable* queryable,
	                         const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                         ibJoinCompareOp onOp,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString);                            // explicit on-cols + comparison (theta when != Eq)
	ibDataQueryBuilder& Join(const ibBackendQueryable* queryable,
	                         const ibQueryColumnExprPtr& onExprL, const ibQueryColumnExprPtr& onExprR,
	                         ibJoinCompareOp onOp,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString);                            // computed ON (a.x+1 <op> b.y) — always RAM theta
	ibDataQueryBuilder& CrossJoin(const ibBackendQueryable* queryable,
	                              ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                              const wxString& alias = wxEmptyString);                        // CROSS / ON TRUE — cartesian
	// Union(b) stacks vertically. keepDuplicates = UNION ALL (default — the historic stack
	// behaviour); false = plain UNION, deduplicating the accumulated rows at this operator.
	ibDataQueryBuilder& Union(const ibBackendQueryable* queryable, const wxString& alias = wxEmptyString,
	                          bool keepDuplicates = true);

	// Owning overloads — see From(shared_ptr) above. A UNION branch and a joined subquery are exactly
	// the sources that die with the run, so this is where the handle matters.
	ibDataQueryBuilder& Union(std::shared_ptr<const ibBackendQueryable> queryable, const wxString& alias = wxEmptyString,
	                          bool keepDuplicates = true) {
		return Union(AdoptOwnedSource(std::move(queryable)), alias, keepDuplicates);
	}
	ibDataQueryBuilder& Join(std::shared_ptr<const ibBackendQueryable> queryable,
	                         const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                         ibJoinCompareOp onOp,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString) {
		return Join(AdoptOwnedSource(std::move(queryable)), onLeft, onRight, onOp, kind, alias);
	}
	ibDataQueryBuilder& Join(std::shared_ptr<const ibBackendQueryable> queryable,
	                         const ibQueryColumnExprPtr& onExprL, const ibQueryColumnExprPtr& onExprR,
	                         ibJoinCompareOp onOp,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString) {
		return Join(AdoptOwnedSource(std::move(queryable)), onExprL, onExprR, onOp, kind, alias);
	}
	ibDataQueryBuilder& CrossJoin(std::shared_ptr<const ibBackendQueryable> queryable,
	                              ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                              const wxString& alias = wxEmptyString) {
		return CrossJoin(AdoptOwnedSource(std::move(queryable)), kind, alias);
	}
	ibDataQueryBuilder& Where(const ibBackendQueryColumn* col,
	                          ibQueryFilterOp comparison, const ibValue& value);             // condition -> predicate (Equal/NotEqual)
	ibDataQueryBuilder& Where(const ibBackendQueryColumn* col, const ibValue& value);        // 2-arg = equality (meta column)
	ibDataQueryBuilder& Where(const ibRawDBColumn& rawColumn, const ibValue& value);         // direct field = value (door owns a copy)
	ibDataQueryBuilder& WhereLike(const ibBackendQueryColumn* col,
	                              const ibValue& pattern);                                    // col LIKE pattern (FindByCode/Description)
	ibDataQueryBuilder& WhereCompare(const ibBackendQueryColumn* col,
	                                 ibQueryFilterOp op, const ibValue& value);               // col <op> value (ordered: <=, <, >, >=, LIKE)
	// A ready-made condition, pushed VERBATIM. The composer re-reads leaves through this door, and a
	// condition is no longer three fields — it carries m_values (In), m_path, m_expr, m_asExists, m_semiJoin.
	// Rebuilding one from (col, op, value) silently drops the rest, which for an `In` means an EMPTY set,
	// i.e. "matches nothing". Forward the whole struct; every future field rides for free.
	ibDataQueryBuilder& Where(const ibQueryCondition& condition);
	// Set membership — col IN (values). THE semi-join key filter: the RAM stitch materialises the cheap side
	// of a join first and pushes that side's key values in here, so the other leaf reads only rows that can
	// possibly join instead of the whole table. NULLs are dropped on the way in (a NULL key matches nothing
	// in an equi-join, and `IN (…, NULL)` is the classic SQL trap), so an all-NULL or empty set renders as
	// "matches nothing" on BOTH the SQL and the RAM side. Pass DISTINCT values — duplicates are harmless but
	// only lengthen the rendered list.
	ibDataQueryBuilder& WhereIn(const ibBackendQueryColumn* col, const std::vector<ibValue>& values);
	// Full boolean WHERE — a predicate TREE (OR / NOT / IS NULL beyond the flat AND-fold of the
	// verbs above). L4 builds it from its parsed WHERE; the provider lowers it to the L2 IR. AND-folded
	// with any verb conditions / row-key filters. (docs/query-language-arc.md §23 — door Where via L2.)
	ibDataQueryBuilder& Where(const ibQueryPredicatePtr& predicate);
	// COMPUTED-expression WHERE — `expr <op> value` (WHERE Qty * Price > 100, a CASE …). A single DB
	// source lowers it server-side via BuildColumnExpr; a COMPUTED source / RAM-filterable predicate
	// evaluates it per row (RamEvalLeaf -> EvalColumnExprRow). Only a multi-source JOIN is gated at L4
	// (GateComputedExpr — the stitch has no cross-leaf expression evaluator).
	ibDataQueryBuilder& WhereExpr(const ibQueryColumnExprPtr& expr,
	                              ibQueryFilterOp comparison, const ibValue& value);   // = / <> (Equal/NotEqual)
	ibDataQueryBuilder& WhereExprCompare(const ibQueryColumnExprPtr& expr,
	                                     ibQueryFilterOp op, const ibValue& value);     // ordered / LIKE
	// Reference DOT-WALK filter / compare — filter the LEAF attribute of a reference path
	// (Producer.Region = value). The provider auto-joins the path (the same join SelectPath builds) and
	// compares on the joined leaf. Single-source read path only. (docs §23 — dot-walk in WHERE.)
	ibDataQueryBuilder& Where(const std::vector<const ibBackendQueryColumn*>& path,
	                          ibQueryFilterOp comparison, const ibValue& value);
	ibDataQueryBuilder& WhereCompare(const std::vector<const ibBackendQueryColumn*>& path,
	                                 ibQueryFilterOp op, const ibValue& value);
	ibDataQueryBuilder& OrderBy(const ibBackendQueryColumn* col, bool ascending);            // col (null = row-key) -> sort
	ibDataQueryBuilder& OrderBy(const std::vector<const ibBackendQueryColumn*>& path, bool ascending); // dot-walk leaf sort
	ibDataQueryBuilder& OrderByExpr(const ibQueryColumnExprPtr& expr, bool ascending);       // ORDER BY <expression> (CASE / arithmetic / value)
	ibDataQueryBuilder& GroupBy(const ibBackendQueryColumn* col);                            // grouping key (reports / totals)
	ibDataQueryBuilder& GroupBy(const std::vector<const ibBackendQueryColumn*>& path);       // dot-walk grouping key (Producer.Region)
	// GROUP BY <expression> — the same expression vocabulary Where/OrderBy/Select/Aggregate already
	// take (arithmetic / CASE / PeriodTrunc). Needs an explicit alias because, unlike a column key,
	// there is no physical name to read the value back by: GetColumn(alias).
	//
	// The case that pulled it in is rolling movements up to a period grain — GROUP BY the period
	// TRUNCATED to month. Grouping by the raw column instead would produce one group per distinct
	// instant, i.e. no rollup at all, so this is not sugar over GroupBy(col).
	ibDataQueryBuilder& GroupByExpr(const ibQueryColumnExprPtr& expr, const wxString& alias);

	// --- dot-walk select columns (reference navigation) ------------------
	// Add an output column that walks a reference PATH and reads a leaf attribute on
	// the related object: SelectPath({Producer, Name}, "prod_name") auto-
	// joins the producer catalog and projects its Name AS prod_name. The path
	// is resolved attributes — every NON-leaf segment is a single-target reference
	// attribute (the door resolves its target queryable and joins on the self-reference
	// column); the leaf is read on the final target. Read it back with GetColumn(alias).
	// Paths sharing a prefix reuse one join. Any target carrying a self-reference join
	// key qualifies (SelfReferenceField — catalogs, documents, charts of characteristic
	// types / accounts); a target without one is rejected, not silently dropped.
	// (docs/query-language-arc.md §22 dot-walk)
	ibDataQueryBuilder& SelectPath(const std::vector<const ibBackendQueryColumn*>& path,
	                               const wxString& alias);

	// --- computed output column (arithmetic / CASE) ----------------------
	// Project a COMPUTED expression (a * b, CASE …) under `alias`; read it back with GetColumn(alias).
	// The provider lowers the L3 expression tree to the L2 IR. Single-source read. (docs §23 computed.)
	ibDataQueryBuilder& SelectExpr(const ibQueryColumnExprPtr& expr, const wxString& alias);

	// --- aggregation (totals / totals) ------------------------------------
	// AggregateFn / AggregateItem / ibTotalLevel are defined at namespace scope (above, before
	// ibDataQueryResult). Re-exposed here as nested aliases for back-compat: callers and the SPEC
	// keep naming ibDataQueryBuilder::AggregateItem / ::AggregateFn. (attr = null for Count -> COUNT(*).)
	using AggregateFn   = ibAggregateFn;
	using AggregateItem = ibAggregateItem;
	using ibTotalLevel  = ::ibTotalLevel;
	using ibTotalField  = ::ibTotalField;

	// HAVING — post-aggregation predicate (stays nested; only the door + provider name it).
	struct HavingItem {
		AggregateFn                 m_fn;
		const ibBackendQueryColumn* m_col;   // null = COUNT(*)
		ibQueryFilterOp             m_op;
		ibValue                     m_value;
	};

	ibDataQueryBuilder& Aggregate(AggregateFn fn, const ibBackendQueryColumn* col, const wxString& alias,
		bool distinct = false);
	// Aggregate over a reference DOT-WALK leaf — SUM(Producer.Weight). The provider joins the path and
	// qualifies the leaf by the join alias; read back by alias like any aggregate.
	ibDataQueryBuilder& Aggregate(AggregateFn fn, const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias,
		bool distinct = false);
	// Aggregate over a COMPUTED expression — SUM(Qty * Price). The provider lowers the input via
	// BuildColumnExpr. Single-source DB aggregates only (gated at the L4 lowering).
	ibDataQueryBuilder& Aggregate(AggregateFn fn, const ibQueryColumnExprPtr& expr, const wxString& alias,
		bool distinct = false);
	ibDataQueryBuilder& Sum  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Sum,   col,     alias); }
	ibDataQueryBuilder& Min  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Min,   col,     alias); }
	ibDataQueryBuilder& Max  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Max,   col,     alias); }
	ibDataQueryBuilder& Avg  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Avg,   col,     alias); }
	ibDataQueryBuilder& Count(const wxString& alias)                                  { return Aggregate(AggregateFn::Count, nullptr, alias); }

	// In-place overloads — the aggregate rolls INTO its own column (no alias). Routed to the CURRENT
	// aggregate context (Group() = the GroupBy set / Totals() = the TotalBy set).
	ibDataQueryBuilder& Sum  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Sum,   col, wxEmptyString); }
	ibDataQueryBuilder& Min  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Min,   col, wxEmptyString); }
	ibDataQueryBuilder& Max  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Max,   col, wxEmptyString); }
	ibDataQueryBuilder& Avg  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Avg,   col, wxEmptyString); }
	ibDataQueryBuilder& Count(const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Count, col, wxEmptyString); }

	// --- totals structure: two aggregate CONTEXTS + dimension levels ----------
	// Group()  switches subsequent .Sum/.Count… into the GROUPBY common aggregate set;
	// Totals() switches them into the TOTALBY common aggregate set. Each set is COMMON across its
	// levels — a level has no own aggregates. (docs/query-language-arc.md §22.1b)
	ibDataQueryBuilder& Group()  { m_aggInTotals = false; return *this; }
	ibDataQueryBuilder& Totals() { m_aggInTotals = true;  return *this; }

	// SELECT DISTINCT — collapse duplicate output rows (SELECT DISTINCT). Applies to the read.
	ibDataQueryBuilder& Distinct() { m_distinct = true; return *this; }

	// SELECT ALLOWED — "give me what I may see, do not refuse me". A refusal from the access
	// policy stops being an exception and becomes an EMPTY read (ibMakeEmptyQueryResult).
	//
	// ⚠ NEVER a default, and the asymmetry is the point: a report that quietly shows fewer rows
	// because of rights is a report that lies quietly, so the platform's normal answer to "you may
	// not read this" is to SAY so. This flag turns that sentence into "there is nothing here",
	// which is only honest when the author asked for it — a list, a choice form, a report over a
	// composite type where one closed branch must not stop the rest.
	ibDataQueryBuilder& Allowed(bool allowed = true) { m_allowed = allowed; return *this; }
	// Row limit for the AGGREGATE terminal (SELECT TOP n + GROUP BY): the DB path renders the
	// dialect LIMIT, the RAM fold truncates after grouping. Plain reads ride the page request
	// instead (ibReadPageRequest::m_count) — this is only for the unpaged SelectAggregate.
	ibDataQueryBuilder& Top(long count) { m_top = count; return *this; }
	// ONE LEVEL, DECLARED WHOLE — its fields and how each unfolds. Levels apply IN ORDER, and a
	// level of several fields groups by the TUPLE of them. This is the door the multi-field level
	// comes through; the two calls below are the one-field spelling of it.
	ibDataQueryBuilder& TotalByLevel(ibTotalLevel level);
	// THE DETAIL RECORDS — the source rows, hung under the deepest heading. Its own verb rather than
	// an empty level handed to the call above, which drops one on purpose (see the .cpp).
	ibDataQueryBuilder& TotalsDetails();
	// One totals dimension level of a single field — the field to roll up by + how it unfolds.
	ibDataQueryBuilder& TotalBy(const ibBackendQueryColumn* col, ibDimensionKind dim);
	ibDataQueryBuilder& TotalBy(const std::vector<const ibBackendQueryColumn*>& path, ibDimensionKind dim);   // dot-walk dimension
	// Dot-walk dimension with a caller-owned SYNTHETIC dimension column: the provider joins the path and
	// projects the leaf's scalar value under `alias` (collision-free vs the main table), and the fold groups
	// by `dimCol` (which reads that alias) under its OWN unique model id — so a self-referential dimension
	// (Parent.Code) does not clash by metaID with the row's own attribute. The caller owns dimCol.
	ibDataQueryBuilder& TotalByDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
		const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim);
	// The same declaration WITHOUT a level of its own — the projection and the spread are registered
	// and the FIELD is handed back, so a caller assembling a level of several fields can put a
	// dot-walk beside a plain column in one key. TotalByDotWalk is this plus TotalByLevel.
	ibTotalField DeclareDimDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
		const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim);
	// THE OVERALL LEVEL — one row above every dimension, folding the whole result.
	//
	// It adds no level and asks for no extra pass: the fold already rolls the whole snapshot into the
	// tree's ROOT. This says the root is WALKED as a row rather than only reachable by GetTotal().
	ibDataQueryBuilder& TotalsOverall(bool on = true) { m_totalsOverall = on; return *this; }

	// HAVING — post-aggregation filter on an aggregate (HAVING SUM(qty) > 100). It
	// references the aggregate EXPRESSION (portable), not the output alias. Several
	// Having() are AND-folded. op = the ordered/LIKE filter op (>, >=, <, <=, LIKE).
	ibDataQueryBuilder& Having(AggregateFn fn, const ibBackendQueryColumn* col,
	                           ibQueryFilterOp op, const ibValue& value);

	// --- explicit output list (multi-source) -----------------------------
	// Select(col, alias) names ONE output column of the (joined / unioned) result, read
	// back by GetColumn(alias). Single-source reads need no select-list (consumers loop
	// attributes via GetValue); a JOIN/UNION composes columns from several leaves, so the
	// output is the select-list — each value pulled from the leaf that owns the column,
	// projected under `alias` (caller-unique). (docs/query-language-arc.md §22.1b)
	ibDataQueryBuilder& Select(const ibBackendQueryColumn* col, const wxString& alias);

	// Row-identity (guidName) lookups — by the row's OWN key, not an attribute.
	// WhereKey: one row; WhereKeyIn: a set (rendered as OR-of-equals).
	ibDataQueryBuilder& WhereKey(const ibGuid& rowGuid);
	ibDataQueryBuilder& WhereKeyIn(const std::vector<ibGuid>& rowGuids);

	// --- write surface (by COLUMN — no statement, no positions) --------
	// SetValue accumulates one COLUMN assignment for INSERT / UPSERT (the values to write,
	// including the key columns: uuid via a raw column, the data-reference / a register's
	// recorder/period/dimensions via their attributes). The same door you read through writes
	// through: From(queryable) + SetValue(col, value)* + Insert()/Upsert(); DELETE keys off
	// Where() only. The UPSERT match is the QUERYABLE's uniqueness key (GetPrimaryKeyColumns — a
	// record's data-reference, a register's composite), NOT a per-column flag. The uuid is the read
	// keyset (the primary key). The provider does the per-column physical decomposition + positional bind
	// DEEP in L2 (ExecuteWrite builds an ibQueryStatement and calls SetValueAttribute); no field
	// / position is ever visible here. The column is the ibBackendQueryColumn abstraction (an
	// attribute IS one; a raw db field — ibRawDBColumn — is one too). (docs §22.1)
	ibDataQueryBuilder& SetValue(const ibBackendQueryColumn* column, const ibValue& value);
	ibDataQueryBuilder& SetValue(const ibRawDBColumn& rawColumn, const ibValue& value);   // direct field (door owns a copy)

	// ACCUMULATE instead of assign — `column = column + delta`, evaluated by the DB. UPDATE only
	// (an INSERT has no current value to add to), and numeric single-field columns only: the point
	// is arithmetic, and a spread column (a reference is two fields) has none.
	//
	// Why it exists rather than read-modify-write on the client: between the read and the write
	// another writer's delta lands, and assignment SILENTLY discards it. The addition happens inside
	// the statement, so concurrent deltas compose instead of overwriting each other — the same
	// property that lets a totals trigger accumulate without coordinating with anyone. Pass a
	// negative delta to subtract.
	//
	// Mechanically it is not a new capability, only a newly reachable one: an ibQueryStatement's
	// values ARE IR expressions (a plain SetValue just happens to put a Const there), so this puts
	// `Add(Col, Const)` in the same slot. No L2 change, no raw SQL.
	ibDataQueryBuilder& AddValue(const ibBackendQueryColumn* column, const ibValue& delta);
	ibDataQueryBuilder& AddValue(const ibRawDBColumn& rawColumn, const ibValue& delta);   // direct field (door owns a copy)

	// CLOSE THE ROW BEING ASSEMBLED AND OPEN THE NEXT — the whole batch surface, one verb.
	//
	// `SetValue(a).SetValue(b).NextRow().SetValue(a).SetValue(b).Insert()` writes TWO rows in ONE
	// statement. Every row must name the same columns in the same order: the columns are the
	// statement's, not the row's, so a row that named different ones would silently write its values
	// into somebody else's fields. The provider REFUSES a ragged set rather than guessing.
	//
	// Insert() is the terminal that batches. Upsert / Update / Delete stay one-row (an UPSERT needs
	// the dialect's own match form, and Firebird's UPDATE OR INSERT takes no SELECT source), so
	// calling this before them raises rather than quietly writing the first row only.
	//
	// Calling it on an EMPTY current row is a no-op, so a caller may end its loop with NextRow()
	// without producing a phantom row of nulls.
	ibDataQueryBuilder& NextRow();

	// How many rows are staged (1 before any NextRow — the degenerate case is still a set).
	std::size_t RowsStaged() const { return m_writeRows.size(); }

	// Effective sort order = user sort ++ the queryable's identity tail (deduped
	// by metaID; the row-key sentinel kept once). This is the cursor's TOTAL
	// order — Select() uses it internally, and a caller that builds a keyset
	// anchor iterates it to produce the per-column anchor values in EXACTLY the
	// order BuildAnchorPredicate binds them.
	static std::vector<ibQuerySortItem> EffectiveSort(const ibBackendQueryable* queryable,
	                                                  const std::vector<ibQuerySortItem>& userSorts);

	// --- terminal --------------------------------------------------------
	// Run the SELECT and open the selection. Borrows the holder through L2,
	// runs, and returns the L3 selection (ibDataQueryResult) whose dtor releases
	// the reservation. For a read query, run + open collapse here (no L2 cursor
	// leaks out). Move-only result.
	[[nodiscard]] ibDataQueryResult Execute(const ibReadPageRequest& request) const;

	// ⭐⭐ THE AGGREGATE, AS A RELATION INSTEAD OF ROWS — the non-terminal ending.
	//
	// Everything about the query is the same; what differs is that nothing runs. A reading that hands
	// this to `GetSourceRelation` becomes a DERIVED TABLE in whoever asked for it, so the join to it,
	// the filter over it and the paging stay ONE statement — instead of materialising the whole
	// result into RAM first and composing against a snapshot.
	//
	// ⚠ It is NOT a second way of building SQL. The relation is assembled by the same provider code
	// the execute path uses (`BuildAggregateQuery`), stopped one step earlier; a hand-rolled
	// `ibQueryRel` beside it would be a second lowering, free to disagree with the first about a
	// join, a spread or a HAVING.
	//
	// Null when this door cannot be composed with — a non-DB source, or a query carrying an access
	// POLICY. The policy is the interesting one: it must fold its restriction into the read it
	// guards, and handing the relation out unrestricted would let a caller compose past it. Such a
	// reading falls back to rows, which answer the same numbers under the same restriction.
	[[nodiscard]] ibQueryRelPtr BuildRelation() const;

	// Build-once overload: reuses `cache` when `signature` matches its last
	// build, otherwise resolves identity + builds IR + renders and refills the
	// cache. Only the external anchor is rebound per tick. `signature` MUST
	// capture every SQL-determining input (the caller owns that contract).
	[[nodiscard]] ibDataQueryResult Execute(const ibReadPageRequest& request,
	                                       ibRenderedPageCache& cache,
	                                       const wxString& signature) const;

	// Aggregated terminal — a FLAT GROUP BY built from GroupBy() + Sum()/Count()/… +
	// Where()/Having(). NOT paged: returns the full grouped set (one row per group). In
	// the result, group columns read through GetValue(attr); aggregate columns through
	// GetColumn(alias). Single-source = physical DB GROUP BY; multi-source = RAM.
	[[nodiscard]] ibDataQueryResult SelectAggregate() const;

	// Paged single-level group read (docs: group-level paging) — GROUP BY + keyset + LIMIT, so a grouping
	// level with thousands of groups (nomenclature hierarchy) pages server-side instead of loading every
	// group. Routes to the provider's ExecuteGroupLevelPage on a pageable shape (CanPageGroupLevel), else
	// the unpaged SelectAggregate. Used by the lowering's single-scalar-dim TOTALS drill.
	[[nodiscard]] ibDataQueryResult SelectAggregatePage(const ibReadPageRequest& page) const;

	// Totals terminal (hierarchical totals) — returns the RAW totals TREE (ibSelectorTree), NOT a
	// flat group set and NOT a paged cursor. The GroupBy() columns are the LEVELS in order
	// (TOTALS field1, field2), Sum()/… the folded aggregates (BY sum1, sum2); the
	// root is the grand total, each level a subtotal node. L3 stops at the tree —
	// flat-vs-hierarchical rendering is the runtime's call (it walks Root()/Node and
	// builds its own model; L3 names no runtime type but ibValue). (docs §22.1b)
	[[nodiscard]] ibSelectorTree SelectTotals() const;

	// The same totals, pushed down to the DBMS and handed back as a RESULT — true when the shape and
	// the driver allow it (see the definition). False = read and fold as before; same tree either
	// way, so this decides cost, not the answer.
	bool TryTotalsPushdown(ibDataQueryResult& out) const;

	// --- introspection (a nested subquery reads its inner query's shape) -----
	// The explicit Select(col, alias) output list (empty = SELECT *), and the PRIMARY
	// source (.From()). A subquery-as-source derives its columns from these: the select
	// list when given, else the primary source's full column set. (docs §22 nested subquery)
	const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& GetSelectColumns() const { return m_selectCols; }
	// …AND THE DOT-WALKED ONES. `SelectPath({Attribute1, Code}, "Attribute1Code")` does not go into
	// the select list — it is recorded here and read back BY ALIAS — so a subquery-as-source that
	// looked only at the list above published none of them, and the outer query naming one was told
	// there is no such attribute. A nested table's columns are its whole output, both kinds.
	const std::vector<ibDotWalkColumn>& GetDotWalks() const { return m_dotWalks; }
	// …AND THE COMPUTED ONES (arithmetic, CASE), for the same reason: a nested table publishes every
	// kind of projection its inner query names, not just the ones that happen to be plain columns.
	const std::vector<ibQueryColumnSelect>& GetSelectExprs() const { return m_selectExprs; }
	const ibBackendQueryable* GetPrimarySource() const { return m_queryable; }

	// Every source LEAF the query reads — the primary From plus every Join / Union branch,
	// in tree order. Access enforcement (RLS) restricts each of them, so the walk must see
	// them all, not just GetPrimarySource(). Fills `sources` (cleared first) and returns
	// whether any were found. (walks the ibQueryNode tree)
	bool GetSources(std::vector<const ibBackendQueryable*>& sources) const;

	// The accumulated WHERE as ONE predicate tree — the .Where(col,op,val) conditions AND-folded with
	// the .Where(tree) predicate (null = no WHERE). Lets a policy OR several roles' restrictions.
	ibQueryPredicatePtr GetWherePredicate() const;

	// RLS `restrict … join …` — attach a SEMI-JOIN restriction (the row passes iff a permitting row EXISTS
	// in the inner). Folded into the WHERE PREDICATE TREE as a payload-carrying leaf — NOT m_conditions —
	// because the write UPDATE / CREATE paths render only m_predicate; the tree rides EVERY WHERE path (read
	// single / co-located / DELETE / UPDATE / CREATE / aggregate) through BuildConditionExpr, so no site can
	// leave a write or a join-query unrestricted. Never a multiplying JOIN, never a separate pre-query.
	void AddSemiJoin(const ibSemiJoinExists& spec) {
		ibQueryCondition c;
		c.m_semiJoin = std::make_shared<ibSemiJoinExists>(spec);
		ibQueryPredicatePtr leaf = ibQueryPredicate::Leaf(c);
		m_predicate = m_predicate
			? ibQueryPredicate::Compose(ibQueryPredicateKind::And, m_predicate, leaf)
			: leaf;
	}

	// Take shared ownership of an on-the-fly source (a subquery / temp wrapper the query references
	// by RAW pointer via From / Join) so it outlives Execute — owned WITH the builder, not on some
	// external value. Returns the raw pointer to hand to From / Join. This is what keeps a subquery
	// SELF-CONTAINED: copying the builder (into ibSubqueryQueryable) carries the ownership, so the
	// subquery's own control block manages everything it references — no raw dependency on the value
	// that produced it (which would dangle if that value died first).
	const ibBackendQueryable* AdoptOwnedSource(std::shared_ptr<const ibBackendQueryable> src);

	// The session connection this builder executes on (default ctor pulls it from ibSession::Current).
	// RLS temp-promote reads it to materialise a computed inner onto THIS connection, so the main
	// SELECT (same connection) sees the temp table — the semi-join then renders server-side.
	ibDatabaseConnectionHolder* GetHolder() const { return m_holder; }
	// Own a temp-table manager for the builder's lifetime (its DROP fires when the builder dies). Kept
	// as shared_ptr so the builder stays COPYABLE (ibSubqueryQueryable copies it) — copies share the temp,
	// and the type-erased deleter means callers destroying a copy never need the complete manager type.
	void AdoptTempManager(std::shared_ptr<ibTempTableManager> mgr) { m_ownedTemps.push_back(std::move(mgr)); }

	// Override the RLS policy (the default ctor already pulls it from the session).
	// Pass nullptr for a TRUSTED / internal read that must bypass enforcement — e.g.
	// a policy's own set-computation query (this is what dissolves re-entrancy).
	ibDataQueryBuilder& WithAccessPolicy(const ibAccessPolicy* policy) { m_policy = policy; return *this; }
	// Aggregate-shape introspection — an AGGREGATE subquery (FROM (SELECT …, SUM(x) … GROUP BY …))
	// derives its exposed columns from these: the GROUP BY keys + one synthetic column per aggregate
	// alias; its ComputeRows runs SelectAggregate instead of a plain read. (docs §23 — agg subquery)
	const std::vector<AggregateItem>&                GetAggregates() const { return m_aggregates; }
	const std::vector<const ibBackendQueryColumn*>&  GetGroupBy()    const { return m_groupBy; }

	// Factory for the opaque build-once cache — the list model owns the result via
	// shared_ptr. Defined in the .cpp where ibRenderedPageCache is complete, so
	// callers never need its layout (header purity).
	static std::shared_ptr<ibRenderedPageCache> NewPageCache();

	// Write terminals — NO arguments: the target row's identity is already in the builder state
	// (the key columns SetValue()'d for INSERT/UPSERT; the Where() conditions for DELETE), and
	// the UPSERT match is the queryable's uniqueness key. Mirrors L2's fluent
	// ibDatabaseQueryBuilder, at the metadata level. False on a DB error.
	//   Insert — INSERT the row from ALL SetValue() assignments (key columns included).
	//   Upsert — INSERT-or-update (the dialect closes the UPSERT spelling); match on the key.
	//   Delete — DELETE the row(s) WHERE the primary key matches (row-key value / key attrs).
	bool Insert() const;
	bool Upsert() const;
	bool Update() const;
	bool Delete() const;

	// A key-only rewrite that touched no row missed its key — raise. A narrowed one (.Where) may
	// legitimately match nothing and stays quiet. See the body for the case that made this necessary.
	void RaiseIfKeyedRewriteMissed(long affected) const;

	// L3-native write kind — the public surface never names the L2 statement
	// (ibQueryStatement::Kind is translated to this only inside the .cpp).
	enum class WriteKind { Insert, Upsert, Update, Delete };

private:
	// The ONE point that appends a left-deep JOIN node carrying `on`; the public Join / CrossJoin overloads
	// assemble an ibJoinOn and route here (no per-overload node-building dance).
	ibDataQueryBuilder& JoinNode(const ibBackendQueryable* queryable, ibQueryJoinKind kind,
	                             const wxString& alias, const ibJoinOn& on);

	// The read lowering (IR build + anchor binds) lives in the DB provider now
	// (ibDbTableProvider in the .cpp), built from this state per Select. The
	// querybuilder holds only the metadata query; see docs/query-language-arc.md §22.
	ibDatabaseConnectionHolder*  m_holder;          // threaded down — L2 borrows it per fetch
	const ibAccessPolicy*        m_policy = nullptr; // RLS — pulled from the session in the default ctor; null = no enforcement
	const ibBackendQueryable*    m_queryable = nullptr;  // .From() — the PRIMARY source (single-source fast path)
	std::vector<std::shared_ptr<const ibBackendQueryable>> m_ownedSources;   // on-the-fly join inners (decorator subquery) kept alive through Execute
	std::vector<std::shared_ptr<ibTempTableManager>>       m_ownedTemps;     // RLS temp-promote — materialised computed inners; DROP on builder death
	std::shared_ptr<ibQueryNode> m_root;            // the relational TREE (.From / .Join / .Union) — the composer executes it
	std::vector<ibQueryCondition> m_conditions;     // .Where() (+ .WhereKey: null-attr = row-key) (+ RLS semi-join payload conditions)
	ibQueryPredicatePtr           m_predicate;      // .Where(tree) — full boolean WHERE (OR/NOT/IS NULL); AND-folded with m_conditions
	std::vector<ibQuerySortItem>  m_sorts;          // .OrderBy() — USER sort; identity tail appended in Select
	std::vector<const ibBackendQueryColumn*> m_groupBy;   // .GroupBy()
	std::vector<std::vector<const ibBackendQueryColumn*>> m_groupPaths;   // parallel to m_groupBy: dot-walk path (empty = plain column)
	// Parallel to m_groupBy as well: a COMPUTED key (null = the plain / dot-walk column). When set,
	// m_groupBy's slot holds nullptr and the alias below names the output — the three vectors stay
	// index-aligned so a provider walks one loop, exactly as it already does for the dot-walk paths.
	std::vector<ibQueryColumnExprPtr> m_groupExprs;
	std::vector<wxString>             m_groupAliases;
	std::vector<ibValue>          m_keyIn;          // .WhereKeyIn() — row-key IN (OR-of-equals)
	// A WRITE IS A SET OF ROWS, AND ONE ROW IS THE DEGENERATE CASE.
	//
	// It used to be a single assignment list, so writing N lines meant N doors, N statements and N
	// round trips — a record set of a thousand lines cost a thousand of each. The vectors below are
	// the same list with one dimension added: SetValue / AddValue fill the LAST row, NextRow() opens
	// another. A caller that never calls NextRow() behaves exactly as before and pays nothing for the
	// dimension, which is why no existing callsite changed.
	std::vector<ibWriteRow>       m_writeRows{ 1 };      // .SetValue() — one assignment list per row
	// Parallel to m_writeRows AND to each row's assignments (both SetValue and AddValue push here):
	// true = this assignment ACCUMULATES (col = col + value) instead of replacing. Same
	// index-aligned-vectors shape the group-by keys already use, so the provider walks one loop.
	std::vector<std::vector<bool>> m_writeAdditive{ 1 };
	std::vector<AggregateItem>    m_aggregates;     // .Group().Sum()… — GroupBy common aggregate set
	std::vector<HavingItem>       m_having;         // .Having()
	std::vector<ibTotalLevel>     m_totals;         // .TotalBy(field, dim) — totals dimensions, in order
	std::vector<AggregateItem>    m_totalAggregates;// .Totals().Sum()… — TotalBy common aggregate set
	bool                          m_totalsOverall = false;   // .TotalsOverall() — the level above every dimension
	bool                          m_aggInTotals = false;  // routing flag: .Aggregate → totals set when Totals() active
	bool                          m_distinct    = false;  // .Distinct() — SELECT DISTINCT
	bool                          m_allowed     = false;  // .Allowed() — a policy refusal yields an EMPTY read instead of throwing
	long                          m_top         = 0;      // .Top() — aggregate-terminal row limit (0 = all groups)
	std::vector<ibDotWalkColumn>  m_dotWalks;       // .SelectPath()
	// The dot-walk dimensions whose leaf is NOT one field: {dimension column id, alias prefix, leaf}.
	// Stamped onto the result (SetObjectRead) so the snapshot reassembles the value instead of asking
	// for a field that was never projected on its own.
	struct ibDimObjectRead { ibMetaID m_dimColumnId; wxString m_alias; const ibBackendQueryColumn* m_leaf; };
	std::vector<ibDimObjectRead>  m_dimObjectReads;
	std::vector<ibDotWalkColumn>  m_dimWalks;       // .TotalByDotWalk() — a dot-walk TOTALS dimension: the provider
	                                                //   joins the path and projects the leaf's SCALAR value under
	                                                //   m_alias (distinct), read by a synthetic dimension column.
	std::vector<ibNamedQuery>     m_with;           // .With(name, inner) — the named queries, in declaration order
	std::vector<ibQueryColumnSelect> m_selectExprs; // .SelectExpr() — computed output columns (arithmetic / CASE)
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> m_selectCols;   // .Select(col, alias) — multi-source output list

	// Raw (direct) columns the door was handed by value — owned here so the column pointers
	// stored above (conditions / writeValues / selectCols) stay valid for the query's life.
	// shared_ptr (not by-value vector) keeps the pointers stable across reallocation AND keeps
	// the door COPYABLE (ibSubqueryQueryable copies the inner builder) — a copied door shares
	// these immutable column descriptors, so the copy's stored pointers stay alive.
	std::vector<std::shared_ptr<ibRawDBColumn>> m_ownedRawColumns;

	// Bundle the accumulated state into the value the provider reads (a short-lived
	// view of refs). The door builds it per terminal and hands it to the vended
	// provider — so the door names no L2 and the provider never reaches into the door.
	ibDataQuerySpec BuildSpec() const;

	// ⚠ ONE EXCEPTION, and it is the NAMED QUERY. A `.With(name, inner)` declaration is a whole door
	// the statement carries, and the provider lowers it into the SAME statement — so it asks that
	// inner door for its spec exactly as the outer one was asked. Friendship rather than a public
	// BuildSpec: the rule ("the door hands its own spec over, nobody takes one") still holds for
	// everybody else.
	friend class ibDbTableProvider;

	// Columns a later result.Select(mode) drains into its snapshot (Select() list + agg inputs + TotalBy fields).
	std::vector<const ibBackendQueryColumn*> MaterialiseColumns() const;
	// Stamp the materialise-columns + totals config onto a freshly executed result.
	void StampResult(ibDataQueryResult& r) const;
};

// An EMPTY selection — a temp table with nothing in it, handed back as an ordinary result.
// The ONE thing L3 vends for a caller that has to answer "there is nothing here" (an ALLOWED
// read whose source the policy closed): building one by hand up there would mean a second
// opinion about what an empty result IS, and there is only room for one.
BACKEND_API [[nodiscard]] ibDataQueryResult ibMakeEmptyQueryResult(const ibBackendQueryable* queryable = nullptr);

// ==========================================================================
// ibDataQuerySpec — the door's accumulated query as a VALUE the provider reads.
// NO "meta" in the name: this is L3 (the door), which is metadata-BLIND — it speaks
// columns / queryable, builds this, and hands it to the provider it pulls from the
// queryable (ibBackendQueryable::GetProvider()). The provider owns ALL the L2 and the
// metadata->physical lowering. A view of refs into the door, valid for the single
// terminal call that built it. (docs/query-language-arc.md §22.2)
// ==========================================================================
struct ibDataQuerySpec
{
	ibDatabaseConnectionHolder*                     m_holder      = nullptr;
	const ibBackendQueryable*                       m_queryable   = nullptr;   // the CURRENT source a leaf provider reads (single-source = the one source)
	const ibQueryNode*                              m_root        = nullptr;   // the relational TREE — the composer walks it (null = pure single-source)
	const std::vector<ibQueryCondition>*            m_conditions  = nullptr;
	ibQueryPredicatePtr                             m_predicate;               // .Where(tree) — full boolean WHERE (null = none)
	const std::vector<ibValue>*                     m_keyIn       = nullptr;
	const std::vector<ibQuerySortItem>*             m_sorts       = nullptr;
	const std::vector<const ibBackendQueryColumn*>* m_groupBy     = nullptr;
	const std::vector<std::vector<const ibBackendQueryColumn*>>* m_groupPaths = nullptr;   // parallel to m_groupBy: dot-walk path per key (empty = plain)
	const std::vector<ibQueryColumnExprPtr>* m_groupExprs   = nullptr;   // parallel: computed key (null = column key)
	const std::vector<wxString>*             m_groupAliases = nullptr;   // parallel: output name of a computed key
	const std::vector<ibDataQueryBuilder::AggregateItem>* m_aggregates = nullptr;
	const std::vector<ibDataQueryBuilder::HavingItem>*    m_having     = nullptr;
	// The rows to write, in order. NEVER empty: a door with no SetValue still carries one empty
	// row, so a reader that wants "the row" says front() and never tests for emptiness first.
	const std::vector<ibWriteRow>*                  m_writeRows     = nullptr;
	const std::vector<std::vector<bool>>*           m_writeAdditive = nullptr;   // parallel to m_writeRows and to each row: col = col + value
	const std::vector<ibDotWalkColumn>*             m_dotWalks    = nullptr;
	const std::vector<ibDotWalkColumn>*             m_dimWalks    = nullptr;   // dot-walk TOTALS dimensions
	const std::vector<ibQueryColumnSelect>*         m_selectExprs = nullptr;   // computed output columns (arithmetic / CASE)
	// The NAMED QUERIES this statement declares (`WITH …`), in declaration order — the provider
	// renders each into the statement's own IR. Empty for every query that names none, which is
	// every query that existed before this.
	const std::vector<ibDataQueryBuilder::ibNamedQuery>* m_with = nullptr;
	const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>* m_selectCols = nullptr;   // multi-source output list
	bool                                            m_distinct    = false;   // .Distinct() — SELECT DISTINCT
	long                                            m_topCount    = 0;       // .Top() — aggregate-terminal row limit (0 = all groups)
};

#endif
