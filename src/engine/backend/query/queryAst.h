#ifndef __QUERY_AST_H__
#define __QUERY_AST_H__

// L4-1 — text query language AST. (Its `ibQueryAstExpr` expression sub-tree is SHARED with
// L4-2: the LINQ lambda recorder builds the SAME expr nodes — compiler/lambdaQueryAst — and
// lowers them through the same builders. `ibQuerySelect` below is L4-1-only.)
//
// A POD tree, L2/L3-FREE (it names ibValue + wxString only, like the L2 IR names
// no metadata). The parser (queryParser.{h,cpp}) builds it; names are NOT yet
// resolved against metadata — resolution happens in lowering (queryLowering.{h,cpp})
// at Execute time, when &parameter values are known. So one parse → many executes
// (the parse is AOT-cacheable on the Query value object).
//
// Expressions are modelled richly (the parser accepts a full SQL-ish predicate);
// the lowering realizes the subset the L3 door supports and throws a clear
// "unsupported" otherwise — keeping the surface honest without bloating the door.
//
// See docs/query-language-arc.md §14 / §23.

#include "backend/compiler/value.h"   // ibValue
#include "queryKeywords.h"            // ibQueryKeyword (aggregate func tag)
#include "queryUnfold.h"              // ibQueryDimUnfold — the three unfold words, shared with L3

#include <vector>
#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

struct ibQuerySelect;   // an Expr may hold a nested SELECT (IN (subquery)); defined in full below

// (ibQueryDimUnfold — the three unfold words — moved to queryUnfold.h, included above: an L3
//  condition may now carry the word too, and this header stays L2/L3-free.)

// What an expression node IS.
enum class ibQueryAstExprKind
{
	Column,    // dotted path a.b.c  (m_path)
	Literal,   // ibValue constant   (m_literal)
	Param,     // &Name              (m_paramName)
	Value,     // value(<Kind>.<Name>.<Member>) — a literal reference / enum / predefined constant, resolved AT
	           // LOWERING against the config (m_path = the dotted meta-path). Behaves like a Param once resolved:
	           // EvalValue turns it into an ibValue that flows as a bound value. (Enum members land here too.)
	Func,      // SUM/MIN/MAX/AVG(arg) | COUNT(*|arg)   (m_func, m_arg / m_star)
	Arith,     // lhs <+ - * / %> rhs   (m_arith)
	Case,      // CASE WHEN p THEN e … [ELSE e] END   (m_cases, m_else)
	// ⭐ CAST(<expr> AS <Kind>.<Name>) — the expression in m_arg, the TARGET TYPE in m_path.
	//
	// It exists for one job, and it is not conversion: a COMPOSITE reference (a register's recorder,
	// a characteristic's value) names several types, so the engine refuses to walk THROUGH it —
	// there is no one set of fields behind it. Naming which type is meant makes the walk possible:
	//
	//     CAST(Recorder AS Document.Order).Number
	//
	// ⚠ A WALK AFTER A CAST IS A COLUMN, not a Cast: `CAST(x AS T).A.B` parses as a Column whose
	// m_path is {A, B} and whose m_arg is the Cast. That is deliberate — the resolver already walks
	// a Column's path from a starting queryable, and the cast only says WHICH queryable to start on.
	// One shape, and every dot-walk mechanism downstream (SelectPath, ExpandDotWalkJoins, the RAM
	// join) works on it unchanged.
	Cast,
	Compare,   // lhs <cmp> rhs      (m_cmp)
	Like,      // lhs [NOT] LIKE rhs (m_negated)
	In,        // lhs [NOT] IN (list | subquery)  (m_negated, m_list / m_subquery)
	IsNull,    // lhs IS [NOT] NULL  (m_negated)
	Between,   // lhs [NOT] BETWEEN low AND high
	Logical,   // lhs (AND|OR) rhs   (m_isOr)
	Not,       // NOT lhs
};

// Comparison operator of a Compare node.
enum class ibQueryCompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// Arithmetic operator of an Arith node (parsed with standard precedence; the column-based L3 door does
// not execute computed expressions yet, so the lowering rejects these — the parser stays complete).
enum class ibQueryArithOp { Add, Sub, Mul, Div, Mod };

// One expression node. A flat tagged struct (only the fields its kind needs are
// populated) — simpler than a class hierarchy for a small AST the lowering walks once.
struct ibQueryAstExpr
{
	ibQueryAstExprKind m_kind = ibQueryAstExprKind::Literal;

	std::vector<wxString> m_path;        // Column: dotted segments
	ibValue               m_literal;     // Literal
	wxString              m_paramName;   // Param

	ibQueryKeyword        m_func = ibQueryKeyword::None;  // Func: Sum/Count/Min/Max/Avg
	bool                  m_star = false;                 // Func: COUNT(*)
	// Func: fold over DISTINCT values of the argument — `COUNT(DISTINCT Board)` asks how many
	// different boards, not how many rows have one.
	//
	// ⚠ NOT the SELECT's own DISTINCT (ibQuerySelect::m_distinct). That one asks whether two whole
	// ROWS are the same; this asks whether two VALUES of one column are — different questions, and
	// a query may perfectly well want one, the other, or both.
	bool                  m_distinctArg = false;
	ibQueryAstExprPtr        m_arg;                          // Func argument

	// Func: the OVER (…) that makes this call a WINDOW — a MODIFIER of the call, held as a field for
	// the same reason m_distinctArg is one, and mirroring how L2-1 carries it (ibQueryExpr::m_over).
	// Null = an ordinary call. Defined below, beside the sort item it holds.
	std::shared_ptr<struct ibQueryAstWindow> m_over;

	ibQueryCompareOp      m_cmp = ibQueryCompareOp::Eq;   // Compare
	ibQueryArithOp        m_arith = ibQueryArithOp::Add;  // Arith
	bool                  m_negated = false;              // Like/In/IsNull/Between negation
	bool                  m_isOr = false;                 // Logical: true=OR, false=AND

	// In: HOW FAR DOWN the operand reaches — the same three words a TOTALS-BY dimension unfolds by,
	// here as a modifier of the set-valued operator (`Account IN HIERARCHY (&Accounts)`). Elements —
	// which is a plain IN — for every other node kind and for every IN written without a word.
	//
	// ⚠ Nothing below L4 sees it: the lowering RESOLVES the subtree into the values it stands for and
	// emits the ordinary IN, so the door and all four drivers keep the one set-valued operator they
	// already render. The word lives where the question is asked, not where the rows are read.
	ibQueryDimUnfold      m_unfold = ibQueryDimUnfold::Elements;

	ibQueryAstExprPtr        m_lhs, m_rhs;                   // Compare/Like/Logical/Not/Arith
	ibQueryAstExprPtr        m_low, m_high;                  // Between
	std::vector<ibQueryAstExprPtr> m_list;                  // In list (value list form)
	std::shared_ptr<ibQuerySelect> m_subquery;           // In (subquery form): lhs IN (SELECT …)

	// CASE: searched WHEN -> THEN pairs (in order) + optional ELSE.
	std::vector<std::pair<ibQueryAstExprPtr, ibQueryAstExprPtr>> m_cases;
	ibQueryAstExprPtr        m_else;

	unsigned int          m_line = 0, m_col = 0;          // source span (diagnostics)

	static ibQueryAstExprPtr Make(ibQueryAstExprKind kind) {
		auto e = std::make_shared<ibQueryAstExpr>();
		e->m_kind = kind;
		return e;
	}
};

// One SELECT output column: an expression (column path or aggregate) + an optional
// alias. SELECT * sets m_star (whole-row, no expr).
struct ibQueryProjection
{
	ibQueryAstExprPtr m_expr;    // column path or aggregate func (null when m_star)
	wxString       m_alias;   // AS alias (empty => derived in lowering)
	bool           m_star = false;
};

// A FROM / JOIN source: EITHER a dotted metaobject path (m_name) OR a nested SELECT
// subquery (m_subquery, set => m_name is empty). "Catalog.Products" -> {Catalog, Products};
// "AccumulationRegister.Goods.Balance" -> {…, Goods, Balance} (the trailing segment may name a
// virtual table). A subquery source: FROM (SELECT …) AS s — lowered via ibSubqueryQueryable.
struct ibQuerySelect;
struct ibQuerySource
{
	std::vector<wxString>            m_name;       // dotted metaobject path (empty when m_subquery set)
	std::shared_ptr<ibQuerySelect>  m_subquery;   // nested SELECT (set => this source is a subquery)
	std::vector<ibQueryAstExprPtr>     m_args;        // source-call args: Balance(&Period, &Filter) — value exprs (param / literal)
	wxString                        m_alias;

	// ⭐ A TABLE HANDED IN FROM OUTSIDE: `FROM &Goods`, where `Goods` is a bound parameter holding a
	// value table. m_name still carries the name (one segment), and THIS says where it comes from.
	//
	// Why a sigil rather than "look it up and see": `&` already means "this came from outside" in
	// every other position in this language, so it means the same thing here — one mark, one
	// meaning. Without it a bare `FROM Goods` could be a temp table the package made, a bound table,
	// or a metaobject, and WHICH ONE would be decided by the order the resolver happens to look. A
	// query has to read as what it is.
	bool                            m_parameter = false;

	// ⭐ WHERE IT WAS WRITTEN. Every expression node carries its span and a source did not, so the
	// one refusal an author meets first — "metaobject 'X' not found or cannot be queried" — arrived
	// at line 0, position 0 while a mistyped FIELD three lines below reported its place exactly.
	// On a forty-line query that is the difference between being told and being sent looking.
	unsigned int                    m_line = 0, m_col = 0;
};

enum class ibQueryJoinKindAst { Inner, Left, Right, Full };

struct ibQueryAstJoin
{
	ibQuerySource      m_source;
	ibQueryJoinKindAst m_kind = ibQueryJoinKindAst::Inner;
	// ON predicate. NULL = no link — the two tables are multiplied (a cross join), which is what
	// "two tables and nothing said about how they meet" means. The one exception is a named
	// reference-path source (`JOIN root.ref AS x`), which carries the relation in its own name.
	ibQueryAstExprPtr     m_on;
};

struct ibQueryOrderItem
{
	ibQueryAstExprPtr m_expr;        // column path (or a reference to a projection alias)
	bool           m_ascending = true;
};

// The frame a windowed AGGREGATE folds over — written as ONE word, never as SQL's full boundary
// clause, because the engine below offers exactly these two and a grammar should not accept a
// sentence nothing can carry out.
//
// Its own enum rather than L2-1's ibQueryFrame: this header is deliberately free of L2 and L3 (the
// same rule ibQueryDimUnfold follows). The lowering maps one to the other, in one place.
enum class ibQueryAstFrame
{
	Unstated,   // no frame word written — a ranking call, or an unordered partition (the whole of it)
	Rows,       // this row and every row before it, counted one by one
	Range       // …and every row sharing this one's sort key, so ties fold together
};

// `OVER (PARTITION BY … ORDER BY … [ROWS|RANGE])`. Empty partition = the whole result is one
// partition; empty order = the call folds the partition unordered, which is what the denominator
// of a share-of-total is.
struct ibQueryAstWindow
{
	std::vector<ibQueryAstExprPtr> m_partitionBy;
	std::vector<ibQueryOrderItem>  m_orderBy;
	ibQueryAstFrame                m_frame = ibQueryAstFrame::Unstated;
};

// (ibQueryDimUnfold — the three unfold words — is declared above the expression node, which now
//  needs it too: the same words are a FILTER modifier as well as a grouping one.)

// ONE FIELD of a TOTALS-BY level: the dimension column + how it unfolds. The unfold belongs to
// the FIELD, not to the level — a level may group by a reference unfolded through its hierarchy
// and by a plain column beside it.
// ⭐⭐ PERIODS(<unit>[, <from>, <to>]) — the field is read as a CALENDAR PERIOD.
//
// `TOTALS SUM(Quantity) BY Period PERIODS(Month, &From, &To)`. Two things follow from that word and
// they are one idea: the level's key is the field TRUNCATED to the unit, and the level is PADDED —
// a month nothing happened in still gets its row. The padding is the reason the word exists at all:
// a report or a chart drawn from rows that only exist where something happened has a hole exactly
// where the reader is looking for a zero.
//
// The UNIT stays a WORD (Day / Month / Quarter / …) — the same vocabulary a register's `Turnovers`
// call takes, read back by the same reader. A periodicity is said in a call, not stored in a field,
// and one vocabulary for it is what keeps the two sayable in one language.
//
// The BOUNDS are OPTIONAL: given, the series is padded to them; left out, it is padded between the
// first and last period the data actually holds. Which is the honest default — with no bounds and
// no data there is no series to speak of.
struct ibQueryTotalPeriods
{
	wxString          m_unit;   // the word, verbatim — the lowering reads it (it owns that vocabulary)
	ibQueryAstExprPtr m_from;   // optional — null = the earliest period in the data
	ibQueryAstExprPtr m_to;     // optional — null = the latest
};

struct ibQueryTotalField
{
	ibQueryAstExprPtr   m_expr;                                  // dimension column path
	ibQueryDimUnfold m_unfold = ibQueryDimUnfold::Elements;
	// Set = this field is grouped BY PERIODS. A shared_ptr rather than a flag beside a unit: the
	// three parts are one answer, and a field either has that answer or has nothing to say.
	std::shared_ptr<ibQueryTotalPeriods> m_periods;
};

// One TOTALS-BY level. Levels apply IN ORDER (each yields a subtotal node; the root is the grand
// total) -> door TotalBy(col, dim).
//
// A level holds ONE OR MORE fields, and its key is the TUPLE of their values: `BY (Partner,
// Contract)` is a SINGLE level asking "partner and contract together", not two nested ones. Which
// of the fields add rows is the DATA's answer, not a distinction the engine draws — a partner's own
// attribute repeats the key it depends on and yields nothing new, a contract genuinely divides it.
// ⭐⭐ ONE RESOURCE OF A TOTALS — the figure and the name it is read back under. The TWIN of
// ibQueryTotalDim below, and it exists for the same reason: a totals tree is read BY NAME, and a
// name the engine derived from the argument is a guess about what the author meant.
//
// 🛑 THE AGGREGATE USED TO TRAVEL BARE, and the name was worked out in the lowering: `COUNT(Number)`
// reads back as `Number`, qualified to `CountNumber` when a LEVEL already claims that name. Which is
// how a cross-table grouped by Number came to head its figures "CountNumber" — a word nobody wrote,
// standing where the reader expects the name of their resource (Max, 2026-08-26: "it should be
// Number at minimum"). A derived name is a fallback; it is not an answer, and it must not out-vote
// one the author gave.
struct ibQueryTotalAggregate
{
	ibQueryAstExprPtr m_expr;    // the aggregate call — SUM(x), COUNT(DISTINCT y), …
	// AS name. Empty = derive it (the argument's own name), which is what every query written
	// before this says and what a person means when they do not name the column.
	wxString          m_alias;

	// ⭐⭐ `OVER <level>` — THE AREA THIS FIGURE IS COMPUTED OVER, and the whole of what the other
	// systems spell as "evaluate an expression in the context of a grouping".
	//
	// Empty = the ordinary aggregate: its area comes FROM THE LADDER, so it means one thing on each
	// heading and follows whatever grouping the reader set. Named = the figure is computed over that
	// level and is CONSTANT inside it — a share's denominator, a group's own total read from a row
	// below it.
	//
	// ⚠ THIS IS NOT A WINDOW OVER ROWS. `TOTALS` runs AFTER the ladder has sorted the rows into
	// levels, so an aggregate here folds NODES: `COUNT(x) OVER Item` answers "how many warehouses
	// has this item", not "how many rows" — a question the row storey cannot even ask, having no
	// nodes yet. That is why the area names a LEVEL and not a list of fields (fields describe an
	// area one storey down, in the selection, where `OVER (PARTITION BY …)` lives).
	//
	// A branch QUALIFIES the level (`ByCharacteristic.Characteristic`) where two branches carry
	// levels of the same name — the role a table name plays before a column. A branch alone is not
	// an area: `SPLIT` does not narrow the rows, so "the total over a branch" is the total of the
	// node it hangs from. (docs/query-language-arc.md §27)
	wxString          m_scope;
};

struct ibQueryTotalDim
{
	std::vector<ibQueryTotalField> m_fields;                     // one or more; empty only mid-build
	// The name this LEVEL is read back under. A totals tree is walked by level and each level's
	// value is asked for by name, so a dimension needs one of its own — without it two levels over
	// the same column (Date by month, Date by day) answer to the same name and one of them wins.
	// Empty = the head field's own column name, which is right for the ordinary single-field case.
	wxString         m_alias;

	// THE HEAD — the level's first field. Callers that legitimately speak of "the level's field"
	// (its icon, the single-field paging gate) ask here rather than indexing into a vector that a
	// half-built level can leave empty.
	const ibQueryTotalField* Head() const { return m_fields.empty() ? nullptr : &m_fields.front(); }
	ibQueryTotalField*       Head()       { return m_fields.empty() ? nullptr : &m_fields.front(); }
	// How the level's head field unfolds. A window shows ONE unfold per level, and it is the head's:
	// only a single-field level can carry a hierarchy at all (the lowering refuses the other case).
	ibQueryDimUnfold HeadUnfold() const { return m_fields.empty() ? ibQueryDimUnfold::Elements : m_fields.front().m_unfold; }
	bool IsSingleField() const { return m_fields.size() == 1; }
};

// ⭐⭐ A NODE THE GROUPINGS SIT ON — `SPLIT … ONTO <name>`.
//
// One of these has always existed and was simply never named: the levels of `ibQuerySelect::m_totalsBy`
// hang on the HIDDEN node every report has. A split is the same thing made visible — it takes a name,
// groupings are hung on it, and the rows are folded down it exactly as they are down the hidden one.
//
// So the same fold runs once per node over ONE read of the source: common levels first, then each
// node's own ladder. Two nodes may group by the SAME field, each with its own totals underneath —
// which is the whole reason a person adds a second one.
struct ibQueryTotalSplit
{
	// What a reader asks this node by (`Selection.Select(ByGroups, "ByCharacteristic")`). Empty: the
	// node still folds, it is simply read by position — which is all an unnamed one can be asked for.
	wxString                     m_name;
	// Its groupings, in order. EMPTY IS LEGITIMATE and means "added, nothing hung on it yet": the
	// node is there to be filled, and a node with nothing on it writes nothing into the query text.
	std::vector<ibQueryTotalDim> m_levels;
};

// The whole SELECT statement.
struct ibQuerySelect
{
	bool                           m_distinct = false;
	bool                           m_selectAll = false;     // SELECT *
	// SELECT ALLOWED — "give me what I may see, do not refuse me". Where the access policy says NO
	// outright, the read yields nothing for that source instead of raising ibBackendAccessException.
	// DELIBERATELY not the default: a refusal is normally TOLD, never mimed as an empty selection, and
	// only the author of a list / a choice form / a composite-type report knows the quiet form is honest
	// here. (docs/query-constructor.md §5 — ALLOWED.)
	bool                           m_allowed = false;
	// FOR UPDATE — this SELECT does not merely read, it HOLDS the rows it returned until the transaction
	// ends. Rendered by the driver dialect (m_rowLockSuffix: FOR UPDATE / WITH LOCK); reaches it as
	// ibReadPageRequest::m_lockForUpdate. The read-then-write pair inside one transaction is what it buys.
	bool                           m_forUpdate = false;
	// INTO <name> — MATERIALISE this result under a name instead of returning it. The statement then
	// yields a ROW COUNT (there is no table to hand back — it went into the temp), and later statements
	// of the same package select FROM that name. Empty = an ordinary select. (docs §5 — query batch.)
	wxString                       m_intoTemp;
	// ONTO <name> — NAME THIS STATEMENT'S RESULT. The result still comes back (unlike INTO, which
	// puts it in a table and hands back a row count); the name is how a reader asks for it —
	// `package["Sales"]` rather than "the third statement" — and how a composition's output says
	// which result it shows. Optional, and usually absent: a lone select needs no name.
	wxString                       m_ontoName;
	// INDEX BY … — the columns the temp table this statement makes is indexed by, so the statements
	// that read it find rows instead of scanning for them. Meaningless without INTO (a table nobody
	// keeps cannot be looked things up in), and the parser says so rather than accepting it quietly.
	std::vector<ibQueryAstExprPtr> m_indexBy;
	long                           m_top = 0;               // SELECT TOP n — row limit on THIS core (0 = none).
	                                                        // On the first core of a UNION it limits the WHOLE
	                                                        // union (like the trailing ORDER BY); on a later
	                                                        // branch / a subquery it limits that branch.
	bool                           m_unionAll = false;      // set on a BRANCH select: attached with UNION ALL
	                                                        // (keep duplicates); plain UNION dedupes the
	                                                        // accumulated rows at that operator (SQL semantics)
	std::vector<ibQueryProjection> m_projections;           // empty + m_selectAll => SELECT *
	ibQuerySource                  m_from;
	std::vector<ibQueryAstJoin>       m_joins;
	ibQueryAstExprPtr                 m_where;                 // null = no filter
	std::vector<ibQueryAstExprPtr>    m_groupBy;               // column-path expressions (flat GROUP BY)
	ibQueryAstExprPtr                 m_having;                // null = none
	std::vector<ibQueryOrderItem>  m_orderBy;

	// TOTALS — hierarchical subtotals (report-style "TOTALS … BY …"). When m_hasTotals,
	// the result is a TREE (door SelectTotals): m_totalsAggregates roll IN-PLACE at
	// every level, m_totalsBy are the dimension levels in order. Distinct from the
	// flat GROUP BY above. (docs/query-language-arc.md §22.1b)
	bool                           m_hasTotals = false;
	std::vector<ibQueryTotalAggregate> m_totalsAggregates;     // SUM(x) [AS name], MAX(y), …
	std::vector<ibQueryTotalDim>   m_totalsBy;              // dimension levels (in order)
	// ⭐⭐ THE VISIBLE NODES — `SPLIT`. There has always been a node here: `m_totalsBy` above is the
	// levels of the one every report has, and nobody had to name it because there was only ever one.
	// A `SPLIT` is a node of exactly that kind, only visible: it carries a name, and groupings sit on
	// it the same way they sit on the hidden one (Max, 2026-08-27).
	//
	// Held as a LIST BESIDE the hidden node's levels rather than as a flag on a level, because that
	// is what it is — a query with no SPLIT has an empty list here and is untouched in every other
	// respect. It also makes an EMPTY node expressible, which is what "add a separator, then hang
	// groupings on it" needs: a node with no levels yet simply writes nothing into the text.
	//
	// ⚠ THE SAME FIELD MAY BE GROUPED IN TWO NODES, and that is the point of them: two splits may
	// each group by Item and roll their own totals underneath. Uniqueness of a grouping field is a
	// question INSIDE a node, never across them.
	std::vector<ibQueryTotalSplit> m_totalsSplits;
	// OVERALL — the level above every dimension: ONE row folding the whole result.
	//
	// ⚠ A FLAG, NOT A LEVEL IN THE LIST ABOVE, and deliberately. A dimension level is a column and a
	// place in an order; the overall is neither — it has no column, and its position is fixed (there
	// is nothing above "everything"). Putting it in the list would add a member whose expression is
	// absent and whose order carries no meaning, and every walk of the dimensions would have to step
	// around it.
	//
	// It asks the runtime for nothing new: the fold already computes the whole-snapshot aggregates
	// into the tree's ROOT (ibQueryComposer::BuildDimensionTree — "grand total in-place"). What this
	// says is whether that root is WALKED as a row, instead of only being reachable by GetTotal().
	bool                           m_totalsOverall = false;

	// UNION — additional SELECT branches stacked vertically (same projection). The first branch is THIS
	// select (its core); each entry here is a further branch. ORDER BY / TOTALS on this select apply to
	// the whole union. (docs §23 — UNION; the composer already stacks leaves.)
	std::vector<std::shared_ptr<ibQuerySelect>> m_unions;
};

using ibQuerySelectPtr = std::shared_ptr<ibQuerySelect>;

// ==========================================================================
// A PACKAGE — several statements written together, sent in ONE trip and answered by an ARRAY of
// results addressed by position (docs/query-constructor.md §5 — query batch). Three properties make
// it more than a transport saving:
//
//   * statements run IN WRITTEN ORDER;
//   * each sees what the previous ones left — in practice a temp table (statement 2 selects INTO
//     `Sales`, statement 3 selects FROM `Sales`);
//   * the returned array is HETEROGENEOUS by position: a plain select yields its table, a
//     select-into-temp yields the row count.
//
// A statement is EITHER a select (possibly INTO a temp table) OR a drop of one. The drop exists
// because a package's temp tables live for the WHOLE package: releasing early inside a long package
// has to be sayable out loud rather than left to scope.
// ==========================================================================
struct ibQueryAstStatement
{
	ibQuerySelectPtr m_select;     // the SELECT (null on a drop statement)
	wxString         m_dropTemp;   // DROP <name> — release a temp table early (empty on a select)

	// ⭐⭐ …OR A LINK, WHICH IS A STEP LIKE THE OTHER TWO. `LINK A INNER JOIN B ON …` RECONCILES two
	// named selections that already ran: rows on either side that found no partner are dropped, and
	// the step answers with HOW MANY it removed (Max, 2026-09-04: *"the LINK operator processes the
	// named selection and itself returns the number of rows it changed"*).
	//
	// It is a statement, and not a clause of the package, because the point of a package is the
	// SEQUENCE (Max: *"the idea of a package is in the sequence"*): a link runs where it is written —
	// after the selections it reconciles, before whatever reads them — and it occupies a position in
	// the returned array like everything else, so a reader can see what it did.
	//
	// ⚠ The package ALSO keeps the whole set in `m_links`: the composer and the query constructor ask
	// what relations exist without running anything (PlacePackageLinks). Two readings of one fact —
	// the step that RUNS it, and the set that DESCRIBES it — kept in step by the parser filling both.
	int              m_linkIndex = -1;   // >= 0: this statement IS the package's link at that index

	bool IsDrop() const { return !m_dropTemp.IsEmpty(); }
	bool IsLink() const { return m_linkIndex >= 0; }
};

// ⭐⭐ A LINK BETWEEN TWO NAMED RESULTS — and it belongs to the PACKAGE, not to any statement in it.
//
// Max, 2026-08-21: mark two statements as named selections and set the links between them, and that
// is all — nothing is added to anybody's tables, nothing is materialised, nothing is substituted.
// A link written INSIDE a statement is an ordinary JOIN and stays what it always was; this is the
// other thing: a relation the package declares BETWEEN the results its statements named.
//
// Both sides are NAMES (`ONTO`), never sources: which statement produced a name is the package's
// own business, and a link that named a source would break the moment a statement moved.
struct ibQueryPackageLink
{
	wxString           m_left;    // the ONTO name on the left of the relation
	wxString           m_right;   // …and on the right
	ibQueryJoinKindAst m_kind = ibQueryJoinKindAst::Inner;
	ibQueryAstExprPtr  m_on;      // the condition; null = declared and not written yet
};

struct ibQueryPackage
{
	std::vector<ibQueryAstStatement> m_statements;

	// The relations between this package's NAMED results, in the order they were written. Empty for
	// every package that declares none, which is every package that existed before this.
	std::vector<ibQueryPackageLink>  m_links;

	// A package of one plain select is what an ordinary query IS — so a caller that
	// only ever wanted a single statement asks for it here rather than indexing.
	ibQuerySelectPtr SingleSelect() const {
		return m_statements.size() == 1 ? m_statements.front().m_select : nullptr;
	}
};

#endif
