#ifndef __QUERY_LOWERING_H__
#define __QUERY_LOWERING_H__

// L4 — lowering: AST + parameter values -> the L3 door (ibDataQueryBuilder),
// executed. This is the metadata layer §14 calls for: it resolves metaobject /
// attribute NAMES against activeMetaData into queryables / columns, maps AST verbs
// onto the door, runs it, and returns the selection plus an OUTPUT SCHEMA (what the
// script sees) for the result wrapper. Runs at EXECUTE time (the &parameter values
// must be known), so one parse feeds many executes.
//
// BOTH L4 front-ends lower through here: L4-1 (the text query language — the parser's
// ibQuerySelect AST via Execute / ExecuteTotals) and L4-2 (LINQ push-down — the lambda
// recorder's ibQueryAstExpr via LowerLambdaPredicate / LowerLambdaColumnPath, reusing
// the same builders; bail = empty, the fold falls back to RAM). L5 (the data composer)
// reaches it only THROUGH rendered L4-1 text — never as a third entry.
//
// The executable subset has long outgrown the MVP: full boolean WHERE (OR / IN /
// IS NULL / NOT, predicate tree), JOIN / subquery sources / UNION, register virtual
// tables with source args, dot-walk across projection / WHERE / ORDER / aggregates,
// flat GROUP BY + HAVING, hierarchical TOTALS, TOP, and the optimizer rewrite pass.
// The realized state lives in docs/query-language-arc.md §23.4 / §23.8 / §23.9 —
// grow that doc, not this list.
//
// See docs/query-language-arc.md §14 / §22 / §23.

#include "queryAst.h"
#include "dataQueryBuilder.h"   // ibDataQueryResult / ibDataQueryBuilder
#include "queryable.h"          // ibBackendQueryColumn

#include <map>
#include <memory>
#include <vector>

class ibQueryLowering
{
public:
	// One output column the script reads back. A plain / dot-walk / aggregate column
	// is read either by source column (GetValue) or by door alias (GetColumn).
	struct OutputColumn
	{
		wxString                    m_name;             // script-visible output name
		const ibBackendQueryColumn* m_col   = nullptr;  // source column (GetValue) — null when by-alias
		wxString                    m_alias;            // door alias (GetColumn) — dot-walk / aggregate / explicit
		bool                        m_byAlias = false;  // read via GetColumn(alias) vs GetValue(col)

		// ⭐ WHAT THIS COLUMN HOLDS — carried even when the value is read BY ALIAS.
		//
		// The three fields above say HOW to read the value; this one says WHAT it is, and the two are
		// different questions. They were the same field until a temp table proved otherwise: a
		// `SELECT … INTO Tmp` over a JOIN projects every column by alias, so m_col was null, so the
		// snapshot's columns were built with no type at all — and a REFERENCE with no type is not a
		// reference. `Tmp.Supplier.Region` then had nothing to walk into, which is exactly what "the
		// temp table does not inherit the type" looks like from the outside.
		//
		// Empty (no clsids) where a type cannot honestly be given: a computed column, a CASE, a
		// literal. Empty means "unknown", never "no type" — a consumer that needs one asks the value.
		ibTypeDescription           m_type;
		// Non-empty for a dot-walk leaf that is a reference / enum / composite: read via
		// GetColumnObject(m_objectPrefix, m_col), which reassembles the object from its prefixed field spread
		// (the provider projects it so). Empty => a plain scalar leaf / aggregate / column read by alias.
		wxString                    m_objectPrefix;
		// A SYNTHETIC source column (a computed TOTALS measure) the lowering built — owned HERE so it
		// outlives the door AND the result: the schema travels with the selection, and m_col points into
		// this. Null for a real metadata column (owned by the metadata) or a by-alias read.
		std::shared_ptr<ibBackendQueryColumn> m_ownedCol;
	};

	// Resolve + build + run. Fills outSchema (in projection order). Throws
	// ibBackendException on a resolution failure, a missing &parameter, or an
	// unsupported construct (with the AST source span). Returns the move-only selection.
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema);

	// RESOLVE THE NAMES, RUN NOTHING. Parsing this language cannot tell a field that exists from one
	// that does not — `DataVersionPredefinedName` is a perfectly legal identifier — because names are
	// resolved against metadata HERE, at execution. So "the query parses" and "the query is sound"
	// are two different statements, and a constructor that reports the first as the second lies
	// quietly until the query is run.
	//
	// This asks the SAME resolver the execution uses (ResolvePath / ResolveColumnSingle) and throws
	// the SAME ibBackendException it would ("unknown attribute 'X'", "ambiguous attribute 'X'"), so
	// there is no second opinion here about what a valid name is.
	//
	// ⚠ It NEVER reports a false error. A source it cannot resolve without running (a package's temp
	// table, a nested query) makes the columns of THAT select unverifiable, and unverifiable is
	// silence — the check descends into the nested query on its own terms instead. A wrong "field not
	// found" would be worse than the gap it closes.
	// BACKEND_API on the method, not the class: this is the first member reached from OUTSIDE the
	// backend DLL (the constructor's verdict line), and exporting the whole class would drag its
	// nested PackageResult — a move-only type holding unique_ptr — across the boundary for nothing.
	BACKEND_API static void CheckNames(const ibQueryPackage& package,
	                                   const std::map<wxString, ibValue>& params);

	// DROP WHAT NO LONGER RESOLVES, and answer how much went. The other half of the same idea: a
	// path is not chased down and deleted when its table is removed — removal simply BREAKS it, and
	// this pass, run whenever the query is refreshed, does not reach it and takes it out.
	//
	// One rule driven by RESOLUTION covers every way a path can break: a table removed, an attribute
	// renamed in the configuration, a metaobject deleted, a temp table no longer made. A cleanup
	// hung off the delete button covers exactly one of them.
	//
	// ⚠ Same promise as CheckNames: what cannot be VERIFIED is never touched. A select whose source
	// cannot be resolved without running (a package temp table, a nested query) is left whole.
	BACKEND_API static int PruneUnresolved(ibQueryPackage& package,
	                                       const std::map<wxString, ibValue>& params);

	// WHICH AGGREGATES CAN BE TAKEN OVER A VALUE OF THIS TYPE — the ONE door, read two ways.
	//
	//   SUM / AVG — a NUMBER. There is no sum of dates and no average of references.
	//   MIN / MAX — anything ORDERED: number, date, string. A reference has no order of its own
	//               (it keys by guid), so folding one would rank rows by an internal identity.
	//   COUNT     — anything at all. Counting asks nothing of the type.
	//
	// ⚠ CheckNames reads it as a REFUSAL ("SUM cannot be taken over 'Description'"); a host reads it
	// as WHAT TO OFFER — the constructor's function cell lists exactly these, so a person is never
	// shown a choice their own engine would then reject. Written twice, the two would drift, and the
	// drift is a dropdown that offers something the query cannot do.
	//
	// A COMPOSITE type (several clsids) yields the full set: which type a row holds is a fact about
	// the ROW, and narrowing on "it might be a string" would be this answer inventing one.
	BACKEND_API static std::vector<ibQueryKeyword> AggregatesFor(const ibTypeDescription& type);

	// WHICH PROJECTIONS THIS QUERY STILL HAS TO GROUP BY — the ONE door for "can this be grouped".
	//
	// Once a query groups (a GROUP BY, or an aggregate anywhere in the projection list) every
	// projected column must be a group key or live inside an aggregate. Returns the ones that are
	// neither, as the AST nodes they are; EMPTY when the query does not group at all, when it is
	// already sound, or when it cannot be verified without running.
	//
	// ⚠ ONE DOOR, TWO READINGS, and that is the point of it being here rather than in a dialog:
	//   * CheckNames READS IT AS A REFUSAL — the query is wrong, and it says which field;
	//   * a host that ASSEMBLES a query reads it as the work still to do — the constructor appends
	//     exactly these when the author groups by one field of several.
	// Written twice, the two would drift, and the drift would be a window that offers a query its
	// own engine then rejects.
	//
	// Why refuse at all, rather than let the database complain: the aggregate terminal builds its
	// SELECT list from the group keys and the aggregates ALONE, so a column that is neither never
	// reaches the database. The schema still declares it, and the caller gets a column with no
	// values and nothing said.
	//
	// Matching is on the RESOLVED leaf, not on the text: `Products.Code` and `Code` are one column
	// when one table owns it.
	BACKEND_API static std::vector<ibQueryAstExprPtr> UngroupedProjections(
		const ibQuerySelect& ast, const std::map<wxString, ibValue>& params);

	// THE COLUMNS THIS QUERY ALREADY FOLDS — the ones living inside an aggregate call.
	//
	// The mirror of the above, and the same two readings. A folded column is accounted for: it is
	// not missing from GROUP BY, and it must not be PUT there — `SUM(Parent) … GROUP BY Parent`
	// leaves one row per group, so the fold returns the value itself.
	//   * CheckNames reads it as a refusal — grouped and aggregated at once is wrong, and it says
	//     which field. It refuses NARROWLY: a column the query also selects plainly is exempt,
	//     because the completeness rule above demands that very key and the two must not disagree;
	//   * a host that assembles a query reads it as "do not offer this one", which is what stops
	//     the same field standing in the grouping list and the aggregates list together.
	//
	// Needs no sources and cannot fail: it reads the written expressions, not what they resolve to.
	// The caller that has to COMPARE it against a group key resolves both (CheckNames does).
	BACKEND_API static std::vector<ibQueryAstExprPtr> AggregatedColumns(const ibQuerySelect& ast);

	// WHAT COLUMNS DOES THIS QUERY PRODUCE — resolved, and NOT RUN.
	//
	// The third question in the same family. `CheckNames` asks whether the names resolve,
	// `PruneUnresolved` drops the ones that stopped, and this one hands back what is left: the output
	// schema, in projection order, exactly as `Execute` would fill it.
	//
	// It matters because a query is a SOURCE before it is a result. A dynamic list over an author's
	// query has to offer those columns in its filters, its sorts, its grouping and its column
	// chooser — and offering them by running the query would mean reading the table to find out what
	// the table is called. The schema is decided by the BUILD, not by the read: the door is populated
	// (which is where names become columns) and then simply not asked for rows.
	//
	// A UNION is described by its FIRST branch, which is not an approximation: the lowering lines the
	// branches up BY NAME, so the first branch names the result. Describing it that way also avoids
	// materialising the others, which is what running the union would do.
	//
	// TOTALS is described by its DETAIL — the clause is set aside and the select underneath answers.
	// The levels of a totals tree are made at the FOLD, and they are not what this is asked for: a
	// host asks "which fields does this query put in front of me", and those are the fields a filter,
	// a sort or a grouping can name.
	//
	// Throws exactly what Execute throws, with the same words and the same source span — so a host
	// showing the query's fields and a host running it cannot disagree about whether it is sound.
	BACKEND_API static void DescribeOutput(const ibQuerySelect& ast,
	                                       const std::map<wxString, ibValue>& params,
	                                       std::vector<OutputColumn>& outSchema);

	// === A PACKAGE — several statements as ONE trip (docs/query-constructor.md §5) ===
	//
	// One entry per statement, IN WRITTEN ORDER, and the array is deliberately
	// HETEROGENEOUS by position, because the statements are:
	//
	//   * a plain select      -> m_result + m_schema (its table)
	//   * a select INTO temp  -> m_rowCount (there is no table to hand back — it went into the
	//                            temp, and the following statements select FROM that name)
	//   * a DROP              -> nothing but the fact that it ran
	//
	// The temp tables live for the WHOLE package (not one execution, as a single query's scope
	// does): that is what lets statement 3 read what statement 2 left, and it is why DROP exists
	// — releasing early inside a long package has to be sayable rather than left to scope.
	struct PackageResult
	{
		wxString m_intoTemp;                                 // non-empty: this statement materialised that temp table
		wxString m_dropTemp;                                 // non-empty: this statement released that temp table
		long     m_rowCount = -1;                            // rows materialised by an INTO statement (-1 = not one)
		bool     m_hasTotals = false;                        // the statement had a TOTALS clause (the result folds)
		std::unique_ptr<ibDataQueryResult> m_result;         // the table of a plain select (null otherwise)
		std::vector<OutputColumn>          m_schema;         // its output columns, in projection order
	};

	// Run every statement of `package` in order and return the results by position. Throws
	// exactly as Execute does — a package fails at the statement that failed, with that
	// statement's own message.
	//
	// `store` — WHO KEEPS THE TEMP TABLES ALIVE. Null: this package owns them for its own run and
	// they die with the call. Non-null (a script's `TempTablesManager`): they outlive it, so a
	// LATER query attached to the same store reads what this one left. Same mechanism, different
	// holder — which is the whole difference between "a batch" and "a set of tables I am keeping".
	static std::vector<PackageResult> ExecutePackage(const ibQueryPackage& package,
	                                                 const std::map<wxString, ibValue>& params,
	                                                 class ibQueryTempTableStore* store = nullptr);

	// The PAGED read — the same lowering with an external page ENVELOPE (anchor /
	// direction / count) threaded into the door's terminal. This is how a paged
	// consumer (the L5 list driver) cursors a query WITHOUT any grammar change; a
	// `TOP n` in the text still caps the page (the smaller count wins). The envelope
	// applies to the plain SELECT path only — an aggregate / UNION read ignores it
	// (no row cursor to anchor).
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema,
	                                 const ibReadPageRequest& page);

	// The paged read WITH the build-once page cache (Lever 1, docs §20): the door
	// reuses the rendered SQL keyed by `signature`, rebinding only the anchor.
	// The caller owns the cache and the signature (it must capture every
	// SQL-determining input — the L5 composer signs its rendered text + params).
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema,
	                                 const ibReadPageRequest& page,
	                                 ibRenderedPageCache& cache, const wxString& signature);

private:
	static ibDataQueryResult ExecuteImpl(const ibQuerySelect& ast,
	                                     const std::map<wxString, ibValue>& params,
	                                     std::vector<OutputColumn>& outSchema,
	                                     const ibReadPageRequest& page,
	                                     ibRenderedPageCache* cache, const wxString& signature);

	// A SELECT WITH NO TABLE — `SELECT 1`, `SELECT &Param`. One row carrying the projected values,
	// built without opening the door at all: there is no source to open it on. A COLUMN in such a
	// query is refused with what is actually wrong ("this query reads no table"), not with "unknown
	// attribute" — the name is not unknown, there is nowhere to read it from.
	static ibDataQueryResult ExecuteSourceless(const ibQuerySelect& ast,
	                                           const std::map<wxString, ibValue>& params,
	                                           std::vector<OutputColumn>& outSchema);

public:

	// Hierarchical TOTALS read — builds the door's Totals()/TotalBy()/aggregates and runs ONE read,
	// returning the result with the totals config STAMPED (the runtime's QueryResult.Select() folds it
	// into a grouped selection — ByGroupsHierarchy — no second query). Fills outSchema (dimension +
	// aggregate columns, in order) for Field / property lookup. Single source. (§22.1b)
	// `page` + `outServerGroupedLevel`: when the TOTALS is a SINGLE plain scalar level over a single source
	// with NO measures (the nomenclature-tree drill) AND page.m_count>0, the level's groups run server-side
	// (GROUP BY dim ORDER BY dim [keyset] LIMIT count) and *outServerGroupedLevel is set true — the caller then
	// emits the flat groups at level 1 WITHOUT the ByGroups fold. Otherwise the usual detail-read + fold (docs:
	// group-level paging). Defaults keep every non-paged / report / multi-level caller on the fold.
	static ibDataQueryResult ExecuteTotals(const ibQuerySelect& ast,
	                                       const std::map<wxString, ibValue>& params,
	                                       std::vector<OutputColumn>& outSchema,
	                                       const ibReadPageRequest& page = ibReadPageRequest{},
	                                       bool* outServerGroupedLevel = nullptr);

	// === L4-2 (LINQ pushdown) — recorded-lambda lowering against ONE source ===
	// The lambda recorder (compiler/lambdaQueryAst.*) emits the same
	// ibQueryAstExpr the text parser does; these wrap the file-local builders so
	// the Queryable fold reuses them verbatim. `captured` maps the lambda's
	// captured outer locals (Param nodes) to their values — the &parameter
	// analogy. Both return EMPTY (null / {}) instead of throwing on anything
	// untranslatable: the fold then falls back to RAM (bail-out, not an error).
	// BACKEND_API: exported so the L4-2 lowering is unit-testable across the DLL (like the
	// ibDbTableProvider::Can* gates) — see tests/test_queryLinqExec.cpp.
	static BACKEND_API ibQueryPredicatePtr LowerLambdaPredicate(const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr,
	                                                const std::map<wxString, ibValue>& captured);
	// A pure column / dot-walk path lambda body (OrderBy / aggregate selectors).
	static std::vector<const ibBackendQueryColumn*> LowerLambdaColumnPath(
	                                                const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr);
	// A computed (arithmetic / CASE) projection lambda body (x => x.A * 2, x => IIF(c, a, b)) —
	// lowered to a server-side output-column expr, read back by its alias. Returns null for a plain
	// column / dot-walk path / structure / chained projection (the caller handles those / falls to RAM).
	static ibQueryColumnExprPtr LowerLambdaColumnExpr(
	                                                const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr,
	                                                const std::map<wxString, ibValue>& captured);
};

#endif
