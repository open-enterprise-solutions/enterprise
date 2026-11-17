////////////////////////////////////////////////////////////////////////////
//	ibQueryLowering::ExecutePackage — several statements as one trip: what prepares,
//	the links between named selections, and the run. Speaks to the translation
//	(queryLowering.cpp) through queryLoweringInternal.h.
////////////////////////////////////////////////////////////////////////////

#include "queryLoweringInternal.h"

#include "queryTempStore.h"               // ibQueryTempTableStore — WHO keeps the temp tables alive
#include "dbTableProvider.h"              // ibCastLeafScope — one query's narrowed fields, at every door
#include "core/query/queryRender.h"       // ibRenderQueryPackage — the package as the journal says it; ibQueryColumnFromPath
#include "core/query/queryRewrite.h"      // ibQueryFlattenAnd — a link's condition, one conjunct at a time

#include <unordered_set>                  // the link step matches rows by a key tuple

using namespace ibLowering;

namespace ibLowering {

// Drain a finished selection into a snapshot, one snapshot column per OUTPUT column.
//
// The ids are minted here, sequentially, rather than borrowed from the source columns: a
// projection may name the same source column twice (under two aliases), and an id that is not
// unique would make the second one overwrite the first. The NAME is what later statements
// select by, so it is the name that must survive — not the provenance of the id.
ibQueryRamTable DrainIntoSnapshot(ibDataQueryResult& result,
                                  const std::vector<ibQueryOutputColumn>& schema)
{
	ibQueryRamTable table;
	std::vector<ibMetaID> ids;
	ids.reserve(schema.size());

	for (size_t i = 0; i < schema.size(); ++i) {
		const ibMetaID id = static_cast<ibMetaID>(i + 1);
		ids.push_back(id);
		// …with whether what it was made from may be shown, which the table keeps: its rows outlive the select.
		table.AddColumn(id, schema[i].m_name, schema[i].GetTypeDesc(), ibString(), schema[i].m_available);
	}

	while (result.Next()) {
		const long row = table.AppendRow();
		for (size_t i = 0; i < schema.size(); ++i) {
			const ibQueryOutputColumn& oc = schema[i];
			ibValue v;
			if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
				v = result.GetColumn(oc.m_objectPrefix, oc.m_col);
			else if (oc.m_byAlias)
				v = result.GetColumn(oc.m_alias);
			else
				v = result.GetValue(oc.m_col);
			table.SetCell(row, ids[i], v);
		}
	}
	return table;
}

} // namespace ibLowering

// ⭐⭐ WHAT PREPARES, RUN — see the header. The COMPOSER's road: it lowers ONE statement of its own
// and needs the tables the statements before it MAKE to be standing when it does.
//
// ⚠ `INTO` AND `ONTO` ARE NOT TWO SPELLINGS OF ONE THING (Max, 2026-08-27: *"INTO is not in the
// link, and it should not be"*). A named result is a QUERY — the lowering declares it to the server
// and nothing runs early; a temp table is ROWS, and rows exist only once somebody has drained them.
// That is why a link relates names and never tables, and why this function executes one of the two
// and passes the other by.
void ibQueryLowering::PreparePackage(const ibQueryPackage& package,
	const std::map<ibString, ibValue>& params,
	ibQueryTempTableStore& store,
	std::map<ibString, std::shared_ptr<const ibBackendQueryable>>& sources)
{
	const ibCastLeafScope castLeaves;   // one query's narrowed fields (ibCastLeafScope) — at every door
	// The names declared SO FAR — a preparing statement may read a selection named above it, which
	// is an ordinary thing to write and costs nothing here (the lowering resolves it).
	std::map<ibString, ibQuerySelectPtr> named;
	ibNamedResultScope namedScope(named);

	for (const ibQueryAstStatement& statement : package.m_statements) {

		if (statement.IsDrop()) {
			// Releasing EARLY — said out loud instead of left to scope, exactly as a package does.
			if (!store.Drop(statement.m_dropTemp))
				ThrowQueryException(0, 0, ibString::Format(
					_("temporary table '%s' does not exist"), statement.m_dropTemp));
			sources.erase(statement.m_dropTemp);   // …and out of the registry with it, not left dangling
			continue;
		}

		if (!statement.m_select)
			continue;
		const ibQuerySelect& ast = *statement.m_select;

		if (ast.m_intoTemp.IsEmpty()) {
			// NOT RUN. A named result is read where it is used; an unnamed statement prepares
			// nothing and is nobody's source — running either here would be work for no reader.
			//
			// ⚠ Registered AFTER it is passed, for the reason ExecutePackage states at length: a
			// name visible while its own statement is lowered resolves to itself, without end.
			if (!ast.m_ontoName.IsEmpty() && !named.emplace(ast.m_ontoName.Lower(), statement.m_select).second)
				ThrowQueryException(0, 0, ibString::Format(
					_("two statements of this package name their result '%s'"), ast.m_ontoName));
			continue;
		}

		if (store.Has(ast.m_intoTemp))
			ThrowQueryException(0, 0, ibString::Format(
				_("temporary table '%s' already exists"), ast.m_intoTemp));
		CheckIndexNamesOutputs(ast);   // before a row is read

		std::vector<ibQueryOutputColumn> schema;
		ibDataQueryResult read = ast.m_hasTotals
			? ExecuteTotals(ast, params, schema, ibReadPageRequest{}, nullptr, /*withDetails*/true)
			: Execute(ast, params, schema);

		ibQueryRamTable snapshot = DrainIntoSnapshot(read, schema);
		const long rows = snapshot.RowCount();

		// INDEX BY — the columns the store builds a lookup over, named as the OUTPUT names, because
		// that is what the statements after it select by.
		std::vector<ibString> indexed;
		for (const ibQueryAstExprPtr& column : ast.m_indexBy)
			if (column && !column->m_path.empty())
				indexed.push_back(column->m_path.back());

		store.Put(ast.m_intoTemp, std::move(snapshot), indexed, ibSourceMetaDataScope::Get());   // its references are the query's config's

		// ⭐ AND INTO THE REGISTRY THE SCOPE IS OPEN OVER, so the next statement — and the query this
		// was all prepared for — resolve the name straight to the table.
		const auto made = store.Sources().find(ast.m_intoTemp);
		if (made != store.Sources().end())
			sources[made->first] = made->second;

		ibJournalInfo(ibT("query"), ibT("prepared '%s': %ld rows"), ast.m_intoTemp, rows);
	}
}

// ⭐⭐ THE PLACEMENT — see the header. `LINK` declares relations as a SET; a query needs a TREE, and
// working the tree out is this function's whole job. It answers for BOTH readers of it: the package
// running here, and the COMPOSER writing its settings over a linked package as text.
ibQueryLowering::ibQueryFromTree ibQueryLowering::PlacePackageLinks(
	const std::vector<ibQueryPackageLink>& links, const std::vector<ibString>& declared)
{
	ibQueryFromTree tree;

	auto isDeclared = [&declared](const ibString& name) {
		for (const ibString& had : declared)
			if (had.IsSameAs(name, false))
				return true;
		return false;
	};

	// A HALF-FILLED ROW SAYS NOTHING YET — the author opened it and has not written it, which is
	// an ordinary state of the window and not a mistake.
	//
	// ⚠ A NAME THIS PACKAGE DOES NOT DECLARE IS A MISTAKE, and it is said out loud: skipping it
	// quietly runs a DIFFERENT query — one link fewer — and hands back numbers nobody can explain.
	// Everywhere else in this lowering an unknown name raises; a link is no exception.
	std::vector<const ibQueryPackageLink*> pending;
	for (const ibQueryPackageLink& link : links) {
		if (link.m_left.IsEmpty() || link.m_right.IsEmpty() || !link.m_on)
			continue;
		for (const ibString& side : { link.m_left, link.m_right })
			if (!isDeclared(side))
				ThrowQueryException(0, 0, ibString::Format(
					_("the link names '%s', which is not a result this package names"), side));
		pending.push_back(&link);
	}
	if (pending.empty())
		return tree;

	std::vector<ibString> present;
	auto reads = [&present](const ibString& name) {
		for (const ibString& had : present)
			if (had.IsSameAs(name, false))
				return true;
		return false;
	};

	// THE LINKS ARE A GRAPH, NOT A LIST, so they are placed until nothing else can be placed —
	// not in one pass down the vector. Written in any order, `[A-B, C-D, B-C]` is one chain; a
	// single pass drops the middle link (neither side placed yet when it is read) and D never
	// enters the query at all, silently.
	while (!pending.empty()) {
		bool placed = false;
		for (size_t i = 0; i < pending.size(); ) {
			const ibQueryPackageLink& link = *pending[i];

			if (present.empty()) {
				tree.m_head = link.m_left;
				present.push_back(link.m_left);
			}
			const bool hasLeft  = reads(link.m_left);
			const bool hasRight = reads(link.m_right);

			if (!hasLeft && !hasRight) { ++i; continue; }   // nothing to hang it off YET

			// ⭐ BOTH SIDES ALREADY IN — this is a SECOND condition between two selections that
			// are already joined, and it must not be dropped: dropping it widens the result by
			// exactly the rows the author wrote it to exclude. It is ANDed into the step that
			// brought the later of them in.
			if (hasLeft && hasRight) {
				for (ibQueryJoinStep& step : tree.m_steps) {
					if (!step.m_name.IsSameAs(link.m_left, false) && !step.m_name.IsSameAs(link.m_right, false))
						continue;
					if (step.m_on) {
						ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
						both->m_isOr = false;
						both->m_lhs  = step.m_on;
						both->m_rhs  = link.m_on;
						step.m_on    = both;
					}
					else {
						step.m_on = link.m_on;
					}
					break;
				}
				pending.erase(pending.begin() + i);
				placed = true;
				continue;
			}

			ibQueryJoinStep step;
			step.m_name = hasLeft ? link.m_right : link.m_left;
			step.m_kind = link.m_kind;
			// SHARED, not copied: whoever lowers this rewrites a deep clone of what it is given, so
			// nothing downstream can reach back into the author's link.
			step.m_on   = link.m_on;
			present.push_back(step.m_name);
			tree.m_steps.push_back(std::move(step));
			pending.erase(pending.begin() + i);
			placed = true;
		}

		// ⚠ NOTHING MOVED AND SOMETHING IS LEFT: what remains relates selections this query does
		// not reach — a second, disconnected group. Joining it in would be a cross product
		// nobody asked for, so the package says so instead.
		if (!placed) {
			ThrowQueryException(0, 0, ibString::Format(
				_("the link between '%s' and '%s' relates selections this package does not join to the rest"),
				pending.front()->m_left, pending.front()->m_right));
		}
	}
	return tree;
}

namespace {

// ⭐⭐ THE LINK, AS A STEP: RECONCILE TWO NAMED SELECTIONS THAT ALREADY RAN.
//
// Max, 2026-09-04: *"without a link the two selections are as they are; with a link even the TOTALS
// change, since we selected them by one another"* — and *"the LINK operator processes the named
// selection and itself returns the number of rows it changed"*.
//
// So it does not build a third table. It takes the rows each side already produced and drops those
// that found no partner on the other; what remains on both sides is the same set of facts, seen
// twice. The totals follow for free: a TOTALS result is folded from its DETAIL rows when it is
// walked, so removing details is all that "the totals change" needs.
//
// ⚠ WHICH SIDE LOSES ROWS is the join kind, read the way anyone reads a join: INNER reconciles BOTH
// sides, LEFT keeps every left row and cleans the right, RIGHT the mirror, FULL removes nothing (it
// asks for everything on both sides, which is what the selections already are).
struct LinkedSide
{
	ibQueryLowering::ibQueryPackageResult* m_result = nullptr;
	ibQueryRamTable                 m_rows;
	std::vector<ibString>           m_keyColumns;   // in step with the other side's list
};

// The columns each side is matched on, read off the ON condition: an AND-chain of `A.x = B.y`.
// Anything else is refused out loud — a link nobody can evaluate must not pass as a link that
// removed nothing.
bool ReadLinkKeys(const ibQueryPackageLink& link, std::vector<ibString>& leftCols,
                  std::vector<ibString>& rightCols, ibString& refusal)
{
	if (!link.m_on) {
		refusal = _("a link needs its ON condition before it can reconcile anything");
		return false;
	}
	std::vector<ibQueryAstExprPtr> terms;
	ibQueryFlattenAnd(link.m_on, terms);

	for (const ibQueryAstExprPtr& term : terms) {
		if (!term || term->m_kind != ibQueryAstExprKind::Compare || term->m_cmp != ibQueryCompareOp::Eq
		 || !term->m_lhs || !term->m_rhs
		 || term->m_lhs->m_kind != ibQueryAstExprKind::Column || term->m_rhs->m_kind != ibQueryAstExprKind::Column
		 || term->m_lhs->m_path.size() != 2 || term->m_rhs->m_path.size() != 2) {
			refusal = _("a link is matched on equalities between the two selections' fields "
			            "(Sales.Item = Plan.Item), joined by AND");
			return false;
		}
		const ibString& lq = term->m_lhs->m_path[0];
		const ibString& rq = term->m_rhs->m_path[0];
		if (lq.IsSameAs(link.m_left, false) && rq.IsSameAs(link.m_right, false)) {
			leftCols.push_back(term->m_lhs->m_path[1]);
			rightCols.push_back(term->m_rhs->m_path[1]);
		}
		else if (lq.IsSameAs(link.m_right, false) && rq.IsSameAs(link.m_left, false)) {
			leftCols.push_back(term->m_rhs->m_path[1]);
			rightCols.push_back(term->m_lhs->m_path[1]);
		}
		else {
			refusal = ibString::Format(
				_("this link relates '%s' and '%s', and its condition names something else"),
				link.m_left, link.m_right);
			return false;
		}
	}
	return !leftCols.empty();
}

// One side's cell, by the OUTPUT name the selection published.
ibValue LinkCell(const ibQueryRamTable& rows, long row, const ibString& column)
{
	for (const ibQueryRamColumn& c : rows.Columns())
		if (c.m_name.IsSameAs(column, false))
			return rows.GetCell(row, c.m_id);
	return ibValue();
}

// …and the whole key of a row: the tuple its side is matched by. Compared as VALUES (ibValueSeqHash),
// never as rendered text — two references that display alike are still two references.
std::vector<ibValue> LinkKey(const ibQueryRamTable& rows, long row, const std::vector<ibString>& columns)
{
	std::vector<ibValue> key;
	key.reserve(columns.size());
	for (const ibString& c : columns)
		key.push_back(LinkCell(rows, row, c));
	return key;
}

// Keep the rows whose key is in `wanted`; returns how many were dropped. The schema is rewritten to
// read BY NAME, because what is handed back is now this table and not the source the rows came from.
long KeepMatched(ibQueryLowering::ibQueryPackageResult& side, const ibQueryRamTable& rows,
                 const std::vector<ibString>& keyColumns,
                 const std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual>& wanted)
{
	ibQueryRamTable kept;
	for (const ibQueryRamColumn& c : rows.Columns())
		kept.AddColumn(c.m_id, c.m_name, c.m_type);

	long dropped = 0;
	for (long r = 0; r < rows.RowCount(); ++r) {
		if (wanted.find(LinkKey(rows, r, keyColumns)) == wanted.end()) { ++dropped; continue; }
		const long into = kept.AppendRow();
		for (const ibQueryRamColumn& c : rows.Columns())
			kept.SetCell(into, c.m_id, rows.GetCell(r, c.m_id));
	}

	side.m_result = std::make_unique<ibDataQueryResult>(std::move(kept), nullptr);
	for (ibQueryOutputColumn& oc : side.m_schema) {
		oc.m_alias   = oc.m_name;   // the snapshot is named by the OUTPUT names — read it by them
		oc.m_byAlias = true;
		oc.m_col     = nullptr;
		oc.m_objectPrefix.clear();
	}
	return dropped;
}

} // namespace

std::vector<ibQueryLowering::ibQueryPackageResult> ibQueryLowering::ExecutePackage(
	const ibQueryPackage& package, const std::map<ibString, ibValue>& params,
	ibQueryTempTableStore* store)
{
	const ibCastLeafScope castLeaves;   // one query's narrowed fields (ibCastLeafScope) — at every door
	std::vector<ibQueryPackageResult> results;

	// ⭐⭐ EVERY QUERY THE PLATFORM RUNS PASSES HERE, and this is where the technology journal sees
	// it. Not at the script's `Query.Execute()` — that door only knows the ones a person typed. A
	// report builds its query, a dynamic list rebuilds one per page, a LINQ block records one from
	// a lambda, a register reads its totals: none of those was ever a string, and all of them arrive
	// at this function.
	//
	// So the AST is RENDERED BACK to text rather than the source being echoed. What is journalled is
	// then what the engine is about to run — after rewriting, after the settings were folded in —
	// which is the query a person actually needs to see when the answer looks wrong.
	ibJournalInfo(ibT("query"), ibT("run:\n%s"), ibRenderQueryPackage(package));

	// WHO KEEPS THE TABLES ALIVE. Without a store the package owns one for its own run: a single
	// query's temp scope is RAII-bound to ONE execution, so statement 3 would never see what
	// statement 2 left. With a store handed in (a script's TempTablesManager), the tables outlive
	// this call and several separate queries share them — the same mechanism, a different holder.
	//
	// The scope holds a POINTER to the map either way, so entries added mid-package are visible to
	// every statement after them, which IS the batch contract.
	// ⚠ SHARED, NOT A LOCAL. The tables die when the last READER lets go, not when this function
	// returns — the results handed back point at this store's columns and are read afterwards. See
	// ibQueryPackageResult::m_temps for what a stack local cost here.
	const std::shared_ptr<ibQueryTempTableStore> ownStore =
		store != nullptr ? nullptr : std::make_shared<ibQueryTempTableStore>();
	ibQueryTempTableStore& temps = store != nullptr ? *store : *ownStore;
	ibTempSourceScope packageScope(temps.Sources());

	// THE RESULTS THIS PACKAGE HAS NAMED so far, by their ONTO name (lower-cased: names in this
	// language are matched without regard to case). A later statement may read one of them, and
	// reading it is what a RESULT LINK is.
	std::map<ibString, ibQuerySelectPtr> named;

	for (const ibQueryAstStatement& statement : package.m_statements) {

		if (statement.IsDrop()) {
			// Releasing EARLY — said out loud instead of left to scope. Dropping a name that was
			// never made is an error, not a shrug: it is either a typo or a statement that was
			// expected to run and did not.
			if (!temps.Drop(statement.m_dropTemp))
				ThrowQueryException(0, 0, ibString::Format(_("temporary table '%s' does not exist"), statement.m_dropTemp));
			ibQueryPackageResult r;
			r.m_dropTemp = statement.m_dropTemp;
			results.push_back(std::move(r));
			continue;
		}

		// ⭐⭐ A LINK RUNS WHERE IT STANDS. It reconciles two selections this package has already
		// named, in place — see ApplyPackageLink's note — and answers from its own position with the
		// number of rows it removed, exactly as an INTO answers with the number it wrote.
		if (statement.IsLink()) {
			ibQueryPackageResult r;

			// ⭐⭐ THE WHOLE SECTION RUNS HERE, not one relation of it. A chain is written with its
			// head said once (`LINK Sales LEFT JOIN Plan ON … JOIN Stock ON …`) and therefore stands
			// in ONE place in the package's sequence — so the statement covers every relation
			// sharing that head, and answers with the total it removed.
			//
			// 🛑 One statement per RELATION was the first shape, and it made the number of
			// statements depend on how many JOINs a section happened to carry — invisible in the
			// text, and it left the second relation of a chain with nobody to execute it once the
			// statement became the section's (2026-09-04).
			const std::size_t firstLink = static_cast<std::size_t>(statement.m_linkIndex);
			const ibString sectionHead = package.m_links[firstLink].m_left;

			long removedInSection = 0;

			for (std::size_t li = firstLink; li < package.m_links.size(); ++li) {

			const ibQueryPackageLink& link = package.m_links[li];
			if (!link.m_left.IsSameAs(sectionHead, false))
				break;

			auto findNamed = [&results](const ibString& name) -> ibQueryPackageResult* {
				for (ibQueryPackageResult& done : results)
					if (!done.m_name.IsEmpty() && done.m_name.IsSameAs(name, false) && done.m_result)
						return &done;
				return nullptr;
			};
			ibQueryPackageResult* left  = findNamed(link.m_left);
			ibQueryPackageResult* right = findNamed(link.m_right);

			// ⚠ SAID, NOT SHRUGGED: a link naming a selection that has not run (or was never named)
			// is a mistake in the text, and answering "removed nothing" would hide it.
			if (left == nullptr || right == nullptr)
				ThrowQueryException(0, 0, ibString::Format(
					_("the link names '%s' and '%s'; a link reconciles selections this package NAMED with "
					  "ONTO, and they must be written before it"), link.m_left, link.m_right));

			std::vector<ibString> leftCols, rightCols;
			ibString refusal;
			if (!ReadLinkKeys(link, leftCols, rightCols, refusal))
				ThrowQueryException(0, 0, refusal);

			ibQueryRamTable leftRows  = DrainIntoSnapshot(*left->m_result,  left->m_schema);
			ibQueryRamTable rightRows = DrainIntoSnapshot(*right->m_result, right->m_schema);

			std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> leftKeys, rightKeys;
			for (long r0 = 0; r0 < leftRows.RowCount(); ++r0)
				leftKeys.insert(LinkKey(leftRows, r0, leftCols));
			for (long r0 = 0; r0 < rightRows.RowCount(); ++r0)
				rightKeys.insert(LinkKey(rightRows, r0, rightCols));

			// Which side the kind cleans — read as any join is read.
			const bool cleanLeft  = link.m_kind == ibQueryJoinKindAst::Inner || link.m_kind == ibQueryJoinKindAst::Right;
			const bool cleanRight = link.m_kind == ibQueryJoinKindAst::Inner || link.m_kind == ibQueryJoinKindAst::Left;

			long removed = 0;
			removed += cleanLeft  ? KeepMatched(*left,  leftRows,  leftCols,  rightKeys)
			                      : KeepMatched(*left,  leftRows,  leftCols,  leftKeys);
			removed += cleanRight ? KeepMatched(*right, rightRows, rightCols, leftKeys)
			                      : KeepMatched(*right, rightRows, rightCols, rightKeys);

			removedInSection += removed;

			ibJournalInfo(ibT("query"), ibT("link %s <-> %s: %ld row(s) removed"),
			              link.m_left, link.m_right, removed);
			}

			// ⭐ THE SECTION'S OWN ANSWER: how many rows this link removed, in total, from the
			// selections it reconciled (Max, 2026-09-04: *"a link returns either Undefined, or how
			// many rows it filtered out"*).
			ibQueryRamTable counted;
			counted.AddColumn(1, ibT("Count"), ibTypeDescription());
			counted.SetCell(counted.AppendRow(), 1, ibValue(static_cast<int>(removedInSection)));
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(counted), nullptr);

			ibQueryOutputColumn oc;
			oc.m_name = ibT("Count"); oc.m_alias = ibT("Count"); oc.m_byAlias = true;
			r.m_schema.push_back(oc);

			results.push_back(std::move(r));
			continue;
		}

		if (!statement.m_select)
			continue;

		// ⭐⭐ QUERY RESULT LINKS (Max, 2026-08-21).
		//
		// A statement may READ a result an earlier statement named with ONTO: `FROM Sales AS S INNER
		// JOIN Plan AS P ON …`. The name is resolved HERE, by putting that statement's own select in
		// as a SUBQUERY — so the link becomes an ordinary join and the whole thing goes to the DBMS
		// as ONE query. Nothing is materialised and nothing comes back to us in between.
		//
		// This is what replaces the "data sets + links between sets" machinery other composition
		// systems grow: there are statements, their results have names, and a link is a join.
		//
		// ⚠ NOT the same as INTO. INTO makes a table (and hands back a row count); a named result
		// stays a result.
		//
		// ⭐⭐ AND THE NAME IS RESOLVED BY THE LOWERING, NOT SUBSTITUTED HERE. It used to be
		// substituted here — the named statement's AST cloned into the reader's FROM as a nested
		// source — and that road has ONE execution model: a nested source is computed in RAM
		// (ibSubqueryQueryable::IsComputedInRam), so the rows came back to us and the join happened
		// here. Which road to take depends on what the ENGINE can do (`WITH`) and on what the named
		// query IS, and both are things only the lowering knows. So the names travel to it in a
		// scope, exactly as transient sources do, and it decides per source (ResolveFrom):
		// declare it to the server, or take its rows.
		ibNamedResultScope namedScope(named);

		// ⚠ A STATEMENT IS LEFT EXACTLY AS IT WAS WRITTEN. A package statement PREPARES a selection;
		// grafting another statement's source into it — which one build of this did — mixes two
		// prepared selections into one query nobody wrote (Max, 2026-08-21, on seeing it). What the
		// links mean is assembled AFTER the statements, as the package's FINAL query (below).
		const ibQuerySelect& ast = *statement.m_select;
		CheckIndexNamesOutputs(ast);   // before a row is read

		ibQueryPackageResult r;
		r.m_hasTotals = ast.m_hasTotals;
		// ONTO — the name this statement gave its result. Carried out with the result so a caller
		// asks by name; an INTO statement never has one (the parser refuses the pair).
		r.m_name = ast.m_ontoName;
		std::vector<ibQueryOutputColumn> schema;
		// ⭐⭐ WITH THE ROWS UNDER THE HEADINGS — always, on this road. A TOTALS result is levels ABOVE
		// data, and the data is the bottom of it: three `BY` levels over a catalogue give four rows
		// per item, the last one being the item itself (Max, 2026-08-22, on a measured result: 284
		// items x 4 = 1136 rows). Without this the deepest heading had nothing under it and a walk
		// that descended to the bottom found an empty selection.
		//
		// The COMPOSER asks per output (it knows whether that output prints rows); a hand-written
		// query has nobody to ask, and its author, having named the levels, means the rows they stand
		// over. Defaulting the other way made the same query text mean two different trees depending
		// on which door it came through.
		//
		// ⚠ THE PRICE, PAID KNOWINGLY: details and the server-side fold are exclusive — `GROUP BY
		// ROLLUP` returns aggregated rows and no detail to hang — so a scripted TOTALS folds in RAM.
		// That is the same road it takes today anyway (Firebird has no ROLLUP), and the big reports
		// go through the composer, which still chooses.
		ibDataQueryResult read = ast.m_hasTotals
			? ExecuteTotals(ast, params, schema, ibReadPageRequest{}, nullptr, /*withDetails*/true)
			: Execute(ast, params, schema);

		// ⚠⚠ REGISTERED AFTER ITS OWN READ, and that is not a detail. The name travels to the
		// lowering in a scope now (ibNamedResultScope), and the lowering RESOLVES a bare source
		// against it — so a name visible while its own statement is being lowered means
		// `SELECT … FROM Sales … ONTO Sales` resolves to itself, and the resolution recurses without
		// end. A statement cannot read the result it is producing; registering afterwards is what
		// says so, and the statements below still see it — they are lowered later.
		//
		// (The temp-table road has always carried the same guard, in its own words: a temp source is
		// resolved as of its MAKER, which is what stops one resolving through itself.)
		// ⚠⚠ AND A NAME IS DECLARED ONCE. Overwriting silently is not a tidier answer: with two
		// statements naming the same result, a reader gets whichever came last, and a package can
		// name a CYCLE (`… ONTO A; … FROM A ONTO B; … FROM B ONTO A`) whose resolution calls itself
		// until the stack ends — a crash, not an error message. The window checks uniqueness as a
		// name is typed; text written by hand and packages built by script come in through here.
		if (!ast.m_ontoName.IsEmpty() && !named.emplace(ast.m_ontoName.Lower(), statement.m_select).second)
			ThrowQueryException(0, 0, ibString::Format(
				_("two statements of this package name their result '%s'"), ast.m_ontoName));

		if (ast.m_intoTemp.IsEmpty()) {
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(read));
			r.m_schema = std::move(schema);
			results.push_back(std::move(r));
			continue;
		}

		// INTO — the rows go into the store under that name, and what comes back is their COUNT.
		// A name declared twice is a mistake worth naming: the second statement would otherwise
		// silently shadow the first and the reader could not tell which one was read afterwards.
		// (With a shared store the clash can also be against a table an EARLIER query left, which
		// is the same mistake seen from further away.)
		if (temps.Has(ast.m_intoTemp))
			ThrowQueryException(0, 0, ibString::Format(_("temporary table '%s' already exists"), ast.m_intoTemp));

		ibQueryRamTable snapshot = DrainIntoSnapshot(read, schema);
		r.m_rowCount = snapshot.RowCount();
		r.m_intoTemp = ast.m_intoTemp;

		// INDEX BY — the columns the store builds a lookup over, named as the OUTPUT names, because
		// that is what the later statements select by. A name the projection did not produce was
		// refused before the read (CheckIndexNamesOutputs).
		std::vector<ibString> indexed;
		for (const ibQueryAstExprPtr& column : ast.m_indexBy)
			if (column && !column->m_path.empty())
				indexed.push_back(column->m_path.back());

		temps.Put(ast.m_intoTemp, std::move(snapshot), indexed, ibSourceMetaDataScope::Get());   // its references are the query's config's

		// WHAT A CREATE-TEMP STATEMENT HANDS BACK: a RESULT of one column and one row, holding the
		// number of records placed in the table — not a bare number.
		//
		// The difference is the caller's. A package's results come back as an array addressed by
		// position, and if one element were a number while the rest are results, every consumer
		// would need a branch to walk it: `Results[i].Select()` has to work for every i. So the
		// count is a one-by-one result, and the array is uniform in KIND. (A drop yields the
		// UNDEFINED value for the same reason seen from the other side: it is a position with no
		// result, and saying so with an empty value is what "no result" IS.)
		{
			ibQueryRamTable counted;
			auto column = std::make_shared<ibSyntheticScalarColumn>(
				ibT("Count"), 0, ibBackendColumnRawDB::RawType::Number);
			counted.AddColumn(column->GetColumnId(), ibT("Count"), ibTypeDescription());
			const long row = counted.AppendRow();
			counted.SetCell(row, column->GetColumnId(), ibValue(static_cast<signed int>(r.m_rowCount)));

			ibQueryOutputColumn oc;
			oc.m_name = ibT("Count");
			oc.m_col  = column;   // the schema travels with the selection — the column must outlive both

			r.m_schema.push_back(std::move(oc));
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(counted), nullptr);
		}

		results.push_back(std::move(r));
	}

	// ⭐⭐ THE FINAL QUERY, ASSEMBLED FROM THE LINKS — and it is the ONLY place two selections meet.
	//
	// Max, 2026-08-21: the statements PREPARE the selections and know nothing about each other; at
	// the end you say how they join, two ready tables are related, and the value goes on. So nothing
	// is grafted into a statement and nothing is materialised: the package's last act is a query
	// whose sources ARE the named selections, joined exactly as the links say.
	//
	// The names resolve through the scope below — declared to the server as `WITH` where the engine
	// reads a named query, read as rows where it does not (ResolveFrom decides). Either way no
	// temporary table is made for a selection, named or otherwise.
	if (!package.m_links.empty() && !named.empty()) {
		ibNamedResultScope namedScope(named);

		// WHERE EACH SELECTION STANDS — worked out by the placer, which is also what the COMPOSER
		// asks when it writes its settings over a linked package. One answer to one question.
		std::vector<ibString> declared;
		for (const auto& entry : named)
			declared.push_back(entry.first);
		const ibQueryFromTree tree = PlacePackageLinks(package.m_links, declared);

		auto asSource = [](const ibString& name) {
			ibQuerySource source;
			source.m_name.push_back(name);
			// NAMED BY ITS OWN NAME, because that is what a link's condition qualifies its fields
			// with (`T1.Ref = T2.Ref`); any other alias would leave the condition naming a source
			// this query does not have.
			source.m_alias = name;
			return source;
		};

		ibQuerySelect final;
		std::vector<ibString> present;

		if (!tree.m_head.IsEmpty()) {
			final.m_from = asSource(tree.m_head);
			present.push_back(tree.m_head);
			for (const ibQueryJoinStep& step : tree.m_steps) {
				ibQueryAstJoin join;
				join.m_source = asSource(step.m_name);
				join.m_kind   = step.m_kind;
				// SHARED, not copied: the lowering rewrites a deep clone of whatever it is given, so
				// nothing downstream can reach back into the author's link.
				join.m_on     = step.m_on;
				final.m_joins.push_back(std::move(join));
				present.push_back(step.m_name);
			}
		}

		// ⭐ WHAT THE FINAL QUERY SELECTS — every selection's fields, each under ITS OWN NAME.
		//
		// `SELECT *` over two joined selections publishes both sides' columns under their bare names,
		// so two `Partner` columns answer to one name and a read by name lands on whichever came
		// first — the right-hand key silently reading the left-hand value. Qualifying by the
		// selection is what the author already writes in the link's condition (`Sales.Partner`), so
		// it is the same vocabulary and not a new one.
		// The names come from the SELECTS THEMSELVES — the statements that produced them are right
		// here, and what a select publishes is what its projections are called. No constructor model
		// is asked: that is the window's view of a query, and this is the engine running one.
		bool everyNameKnown = true;
		for (const ibString& name : present) {
			const auto found = named.find(name.Lower());
			const ibQuerySelectPtr source = found != named.end() ? found->second : nullptr;
			if (source == nullptr || source->m_selectAll || source->m_projections.empty()) {
				everyNameKnown = false;   // `SELECT *` inside — its fields are not named here
				break;
			}
			int index = 0;
			for (const ibQueryProjection& projection : source->m_projections) {
				if (projection.m_star || !projection.m_expr)
					continue;
				const ibString field = OutputNameFor(*source, projection, index++);
				if (field.IsEmpty())
					continue;
				ibQueryProjection qualified;
				qualified.m_expr  = ibQueryColumnFromPath(name + ibT(".") + field);
				qualified.m_alias = name + ibT("_") + field;
				final.m_projections.push_back(std::move(qualified));
			}
		}
		// ⚠ ALL OR NOTHING. A half-qualified list would publish one side by name and the other not at
		// all; the star is the honest fallback when a selection does not name its own fields, and the
		// duplicate-name hazard is then the author's own `SELECT *`, visible in their text.
		if (!everyNameKnown) {
			final.m_projections.clear();
			final.m_selectAll = true;
		}

		// ⛔ AND NO THIRD TABLE IS PRODUCED HERE ANY MORE.
		//
		// This road used to append a FINAL result — the query assembled from the links — and that was
		// the wrong shape twice over. It answered a position no statement was written at, in an array
		// whose whole contract is "one entry per statement"; and it left the named selections exactly
		// as they were, so the author saw their own two results unchanged beside a third they had not
		// asked for (Max, 2026-09-04: *"it is more logical to work with the ones that exist than to
		// pile on something new"* — and the link "returns the number of rows it changed").
		//
		// A LINK is a STEP now (see the statement branch above): it reconciles the two selections in
		// place and answers with what it removed. The tree built here is still worth building — the
		// composer and the constructor ask for exactly this placement (PlacePackageLinks) — so the
		// walk above stays and only the extra result is gone.
	}

	// ⭐ AND EVERY RESULT TAKES A SHARE OF THE TABLES IT READS FROM — stated ONCE, here, rather than
	// at each of the four places a result is built: a share that has to be remembered per branch is
	// a share the next branch forgets. Null store = a caller's TempTablesManager owns them already.
	if (ownStore)
		for (ibQueryPackageResult& r : results)
			r.m_temps = ownStore;

	return results;
}
