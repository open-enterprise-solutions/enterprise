////////////////////////////////////////////////////////////////////////////
//	NAME CHECK — resolve, run nothing (ibQueryLowering::CheckNames, PruneUnresolved,
//	UngroupedProjections). Asks the same resolver the execution asks (queryLowering.cpp);
//	speaks to it through queryLoweringInternal.h.
////////////////////////////////////////////////////////////////////////////

#include "queryLoweringInternal.h"

#include "backend/query/queryException.h"           // ibBackendQueryNameException — a name the application does not have
#include "backend/query/columnLayout.h"             // DescribeColumnLayout — whether a field can be compared at all
#include "core/query/queryRewrite.h"                // ibQueryRewrite — the check sees the query the execution runs
#include "core/query/queryRender.h"                 // ibQueryOutputName — the ONE answer to "what is this field called"
#include "core/query/queryAggregates.h"             // ibQueryAggregatesFor — what a type does not admit is refused
#include "core/stringTokenizer.h"                   // the OVER area may name several groupings, comma-separated

#include <algorithm>
#include <functional>   // the recursive walks over an expression (std::function)

using namespace ibLowering;

namespace ibLowering {

// Every COLUMN expression under a node, in reading order. The check walks these and asks the same
// resolver the execution asks; it does NOT descend into a nested SELECT, which is checked on its
// own terms (with its OWN sources — a column inside a subquery belongs to that subquery's tables).
void CollectColumns(const ibQueryAstExprPtr& e, std::vector<ibQueryAstExprPtr>& out)
{
	if (!e)
		return;
	// A COLUMN ROOTED ON A CAST is collected WHOLE — ResolvePath knows that shape and resolves both
	// halves (the narrowed field here, the trailing walk on the target). Descending into it would
	// hand the checker a path with no source to start on.
	if (e->m_kind == ibQueryAstExprKind::Column) { out.push_back(e); return; }

	// The one walk, minus the words (queryAST.h). This collector spelled its own list of children and
	// was the walker that never learned m_args: a column inside `YEAR(Period)` or `DATEDIFF(a, b, Day)`
	// went unchecked here while the execution resolved it.
	ibQueryForEachOperand(*e, [&out](const ibQueryAstExprPtr& child) { CollectColumns(child, out); });
}

void CheckSelectNames(const ibQuerySelect& ast, const std::map<ibString, ibValue>& params);

// The sources of one select, resolved for CHECKING. Anything that cannot be resolved without
// RUNNING — a package's temp table, a nested query — makes this select unverifiable and the check
// falls silent on it rather than inventing an error. The nested query is still checked itself.
//
// ⚠ ONE DOOR, TWO READINGS — and only ONE of them is allowed to accuse. CheckNames is the VERDICT:
// a qualified name the factory does not know is a table that is GONE and it says so, loudly. Its
// two siblings (PruneUnresolved, UngroupedProjections) are asking what WORK IS LEFT, and their
// published promise is that the unverifiable answers empty. Raising at them turns "I cannot check
// this" into an exception the caller never asked for — with no application data at all (a headless
// host, a bare test binary) the very first source raises and a plain `SELECT Code, Name` — which
// owes no grouping whatsoever — dies before the rule is even read.
//
// So the accusation is the CALLER's to ask for. `reportMissing` says which reading this is; the
// silent one still returns false, which every caller already handles as "nothing can be said".

// A source this check cannot know the columns of — held in the list so positions stay true, with
// nothing behind it. Every reader here already skips a binding with no queryable.
ibSourceBinding Opaque(const ibQuerySource& source)
{
	ibSourceBinding binding;
	binding.m_alias = ibQuerySourceName(source);
	return binding;   // m_q stays null — that is what makes it opaque
}
// ⭐⭐ AND ONE UNVERIFIABLE SOURCE DOES NOT MAKE A QUERY UNVERIFIABLE.
//
// This answered "cannot verify" for the WHOLE query the moment ONE source was a nested select — and
// a nested select is an ordinary thing to write: `FROM (SELECT …) AS T, AccumulationRegister.R.
// BalanceAndTurnovers(…)`. Everything standing beside it went unchecked with it, so a field the
// register plainly does not produce sailed through and the verdict line said the engine reads the
// query. The one sentence this window must never say untruthfully, said because of the table NEXT to
// the one at fault.
//
// `tolerateOpaque` keeps the unverifiable source IN THE LIST instead, with no queryable behind it:
// its position is preserved (the join rules read sources by position), its columns are left alone,
// and every other source is checked exactly as strictly as before.
bool BuildCheckSources(const ibQuerySelect& ast, const std::map<ibString, ibValue>& params,
                       std::vector<ibSourceBinding>& out, bool reportMissing = true,
                       bool tolerateOpaque = false)
{
	std::vector<const ibQuerySource*> sources;
	sources.push_back(&ast.m_from);
	for (const ibQueryAstJoin& join : ast.m_joins)
		sources.push_back(&join.m_source);

	for (const ibQuerySource* source : sources) {
		if (source == nullptr)
			return false;

		if (source->m_subquery) {
			// Same split: the nested select is VERDICT work. A caller that only asked what is left
			// to do must not be handed the nested query's refusal either.
			if (reportMissing)
				CheckSelectNames(*source->m_subquery, params);   // on its own terms
			// Its COLUMNS are not ours to verify — the sources beside it still are.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}
		if (source->m_name.empty()) {
			// A source still being written: unverifiable, not wrong. Same treatment.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}

		// ⚠ TWO DIFFERENT SILENCES, and telling them apart is the whole point.
		//
		// A ONE-SEGMENT name is a temp table. It may legitimately be made by a statement this check
		// cannot see (another package, a caller's manager), so failing to resolve it means "cannot
		// verify" — and inventing an error there would be worse than saying nothing.
		//
		// A QUALIFIED name (Kind.Name) is a metaobject, resolved through the application's own factory.
		// If the factory does not know it, the object is GONE — deleted or renamed — and that is not
		// "cannot verify", that is the query naming something that does not exist. Swallowing it is
		// how a query goes on looking healthy after the table under it was deleted: the fields are
		// still listed, the verdict line still says the engine reads the query, and the first sign
		// of trouble arrives when somebody runs it.
		std::shared_ptr<const ibBackendQueryable> queryable;
		try { queryable = ResolveSource(*source, params); }
		catch (const ibCoreException&) {
			if (reportMissing && source->m_name.size() > 1)
				throw;      // the engine's own words, its own position — carried up verbatim
			// A temp table this check cannot see — or a reading that does not accuse.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}
		if (queryable == nullptr) {
			// A name nothing answers to is the QUERY being wrong, not the engine — L4's variety even
			// though L3 is the one that finds out, because that is who the message is for.
			if (reportMissing && source->m_name.size() > 1)
				ibBackendQueryNameException::ErrorAt(source->m_line, source->m_col,
					_("Table '%s' does not exist"), ibQuerySourceName(*source));
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}

		// ⭐⭐ THE NAME A SOURCE ANSWERS TO IS ibQuerySourceName — the alias if one is written, the
		// last segment of the name if not. That is what the renderer writes, what the constructor
		// matches on, and what the author therefore SEES; binding by `m_alias` alone meant this check
		// could not attribute `BalanceAndTurnovers.Period` to the table one line above it unless the
		// author had happened to write `AS`. Two readers of one sentence, and only one of them right.
		ibSourceBinding binding;
		binding.m_alias = ibQuerySourceName(*source);
		binding.m_q     = queryable;   // a virtual table's reading is this check's own
		out.push_back(binding);
	}
	// At least one source this check can actually stand on. All-opaque is the old silence, unchanged.
	for (const ibSourceBinding& binding : out)
		if (binding.m_q != nullptr)
			return true;
	return false;
}

// WHICH PROJECTIONS ARE NEITHER A GROUP KEY NOR INSIDE AN AGGREGATE — the rule itself, over sources
// that are already resolved. Its two readings (a refusal, and the work still to do) are the public
// entries; this is what both of them ask. See ibQueryLowering::UngroupedProjections.
// The walk is the core's (ibQueryCollectFoldedAndFree, queryRewrite.h): which columns a projection folds and which it
// reads free, one question about one tree — the constructor's windows ask it too.
std::vector<ibQueryAstExprPtr> CollectUngrouped(const ibQuerySelect& ast,
                                                const std::vector<ibSourceBinding>& sources)
{
	std::vector<ibQueryAstExprPtr> out;

	// DOES IT GROUP AT ALL? A GROUP BY says so outright; so does an aggregate ANYWHERE in the
	// projection list, because one aggregate makes the whole SELECT a fold. Anywhere, not at the
	// top: `SUM(Qty) / COUNT(*)` is as much a fold as `SUM(Qty)` is.
	// ⚠ WALKED ONCE. Each projection's tree is read a single time and both answers kept — the sweep
	// for "does it group at all" used to walk every projection, and the loop below walked them all
	// again. Two identical passes over the same trees, on a check that runs on every keystroke.
	//
	// THE COLUMNS A PROJECTION READS FREE are the ones outside every aggregate. For `Qty` that is Qty
	// itself; for `SUM(Qty)` there are none; for `SUM(Qty) / Price` it is Price alone — exactly the
	// field that has to be a group key, and the one the old shape never looked at.
	bool grouping = !ast.m_groupBy.empty();
	// The COMPUTED keys, in the text the execution matches them by (see ibQueryCollectFoldedAndFree).
	std::vector<ibString> groupedExprs;
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		if (key && IsComputedExprAst(*key))
			groupedExprs.push_back(ibRenderQueryExpr(*key));
	std::vector<std::vector<ibQueryAstExprPtr>> freePerProjection;
	freePerProjection.reserve(ast.m_projections.size());
	for (const ibQueryProjection& projection : ast.m_projections) {
		std::vector<ibQueryAstExprPtr> folded, free;
		ibQueryCollectFoldedAndFree(projection.m_expr, false, folded, free, &groupedExprs);
		if (!folded.empty())
			grouping = true;
		freePerProjection.push_back(std::move(free));
	}
	if (!grouping || ast.m_selectAll)
		return out;   // not a grouping query, or one whose columns are not written out to judge

	// The group keys resolve ONCE, not once per projected column.
	std::vector<std::vector<std::shared_ptr<const ibBackendQueryColumn>>> keyCols;
	keyCols.reserve(ast.m_groupBy.size());
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		keyCols.push_back(key && key->m_kind == ibQueryAstExprKind::Column && !key->m_path.empty()
			? ResolvePath(sources, *key) : std::vector<std::shared_ptr<const ibBackendQueryColumn>>());

	for (const std::vector<ibQueryAstExprPtr>& free : freePerProjection) {
		for (const ibQueryAstExprPtr& column : free) {
			if (column == nullptr || column->m_path.empty())
				continue;
			const std::vector<std::shared_ptr<const ibBackendQueryColumn>> projCols = ResolvePath(sources, *column);
			bool grouped = false;
			// SAME COLUMN, whichever way it is written: `Products.Code` and `Code` are one column
			// when one table owns it, so the comparison is on the resolved LEAF, not on the text.
			//
			// ⭐ …AND A KEY COVERS WHAT IT WALKS INTO. `GROUP BY Producer` fixes `Producer.Region` for
			// every row of the group, so a key that is a PREFIX of the projected path is enough. The
			// lowering adds the leaf to the server's GROUP BY itself (a spelling SQL requires); asking
			// the author for it here would be demanding he repeat what he has already said.
			for (const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& key : keyCols) {
				if (key.empty() || key.size() > projCols.size())
					continue;
				bool prefix = true;
				for (size_t k = 0; k < key.size() && prefix; ++k)
					prefix = (key[k] == projCols[k]);
				if (prefix) { grouped = true; break; }
			}
			// ⭐ …AND THE SAME WORDS ARE THE SAME COLUMN. Within one query `M.Month` in the projection and
			// `M.Month` in GROUP BY name one field, whatever the resolution hands back for each. For a
			// column of a SUBQUERY source standing in a JOIN it handed back no match, and the check refused
			// a grouping the execution ran without a word: "'Month' is neither grouped nor aggregated",
			// with `GROUP BY M.Month` in the text (a receivables report over months joined to turnovers,
			// 2026-09-11). The resolved comparison above stays the first answer — it is what equates two
			// spellings of one column; this one only stops identical spellings from being told apart.
			for (size_t i = 0; !grouped && i < ast.m_groupBy.size(); ++i) {
				const ibQueryAstExprPtr& key = ast.m_groupBy[i];
				if (!key || key->m_kind != ibQueryAstExprKind::Column || key->m_path.size() != column->m_path.size())
					continue;
				bool same = true;
				for (size_t k = 0; k < key->m_path.size() && same; ++k)
					same = key->m_path[k].IsSameAs(column->m_path[k], false);
				grouped = same;
			}
			if (!grouped)
				out.push_back(ibQueryColumnCopy(*column));
		}
	}
	return out;
}

// ⚠⚠ AN INCOMPLETE GROUPING IS REFUSED — asked of the SAME door a host asks to complete one, so what the
// check calls wrong and what the constructor fixes are one answer (see the header). Said by the check
// (CheckSelectNames) and by every query that RUNS (PopulateBuilder) in these words.
void RefuseUngrouped(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources)
{
	const std::vector<ibQueryAstExprPtr> ungrouped = CollectUngrouped(ast, sources);
	if (ungrouped.empty())
		return;
	const ibQueryAstExprPtr& first = ungrouped.front();
	ThrowQueryException(first->m_line, first->m_col, ibString::Format(
		_("'%s' is neither grouped nor aggregated: add it to GROUP BY, or wrap it in an aggregate"),
		first->m_path.back()));
}

// ⭐⭐ A STRING OF UNLIMITED LENGTH IS COMPARED ONLY CAST TO A LENGTH — `CAST(Comment AS String(100))`, its first
// hundred characters, and the language says so for every engine alike (Max, 2026-11-10). Its column is a text of no
// size (ibCanonicalKind::Text), a text blob on Firebird that is neither sorted nor grouped there, so a bare one stands
// in no comparison, IN or BETWEEN, no ORDER BY, GROUP BY or DISTINCT, no MIN, MAX or COUNT(DISTINCT). Read, LIKE,
// IS NULL, SUBSTRING and the CAST itself take it as it is. Said by the check (CheckSelectNames) and by every query
// that RUNS (PopulateBuilder), as RefuseUngrouped is.
void CheckUnlimitedStringsAreCast(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources)
{
	// Asked of the column's LAYOUT, not of its length: a raw column's 0 is the 255 it was always given. A name that
	// does not resolve is the name check's to report, in its own words — silence here.
	const auto refuse = [&sources](const ibQueryAstExprPtr& e, const ibQueryAstExprPtr& at = nullptr) {
		if (!e || e->m_kind != ibQueryAstExprKind::Column || e->m_path.empty())
			return;
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
		try { cols = ResolvePath(sources, *e); }
		catch (const ibCoreException&) { return; }
		if (cols.empty())
			return;
		// A column of NO TYPE is no string of unlimited length: it holds any value, laid out as the widest composite, whose
		// string field has no limit because nothing declared one (DescribeColumnLayout). Its keys order and group (Max,
		// 2026-11-18). No table of an application has one — a value table's, a computed source's, a copy's — and those are
		// read where a text compares: a base in memory, a temporary table, never a Firebird blob.
		if (cols.back()->GetTypeValueDesc().GetClsidCount() == 0)
			return;
		const std::vector<ibColumnSlot> layout = DescribeColumnLayout(*cols.back());
		if (std::none_of(layout.begin(), layout.end(),
				[](const ibColumnSlot& slot) { return slot.m_type.m_kind == ibCanonicalKind::Text; }))
			return;
		const ibString name = ibRenderQueryExpr(*e);
		const ibQueryAstExpr& place = at ? *at : *e;
		ThrowQueryException(place.m_line, place.m_col, ibString::Format(
			_("'%s' is a string of unlimited length: it is compared, ordered and grouped only cast to a length - CAST(%s AS String(100))"),
			name, name));
	};
	// …wherever it is compared, or folded to one value, at any depth of an expression.
	std::function<void(const ibQueryAstExprPtr&)> walk = [&walk, &refuse](const ibQueryAstExprPtr& e) {
		if (!e)
			return;
		switch (e->m_kind) {
		case ibQueryAstExprKind::Compare:
			refuse(e->m_lhs);
			refuse(e->m_rhs);
			break;
		case ibQueryAstExprKind::In:
			refuse(e->m_lhs);
			for (const ibQueryAstExprPtr& item : e->m_list)
				refuse(item);
			break;
		case ibQueryAstExprKind::Between:
			refuse(e->m_lhs);
			refuse(e->m_low);
			refuse(e->m_high);
			break;
		case ibQueryAstExprKind::Func:
			if (e->m_func == ibQueryKeyword::Min || e->m_func == ibQueryKeyword::Max || e->m_distinctArg)
				refuse(e->m_arg);
			break;
		default:
			break;
		}
		ibQueryForEachOperand(*e, [&walk](const ibQueryAstExprPtr& child) { walk(child); });
	};

	for (const ibQueryProjection& projection : ast.m_projections) {
		if (ast.m_distinct)
			refuse(projection.m_expr);
		walk(projection.m_expr);
	}
	walk(ast.m_where);
	walk(ast.m_having);
	for (const ibQueryAstJoin& join : ast.m_joins)
		walk(join.m_on);
	for (const ibQueryAstExprPtr& key : ast.m_groupBy) {
		refuse(key);
		walk(key);
	}
	// An ORDER BY may name an OUTPUT (CheckSelectNames): `ORDER BY Comment` over `CAST(Comment AS String(100)) AS
	// Comment` sorts the cast, not the column it shares a name with. Either way it is said at the ORDER BY's name: the
	// sort is asked there.
	for (const ibQueryOrderItem& item : ast.m_orderBy) {
		const ibQueryAstExprPtr& e = item.m_expr;
		const auto output = e && e->m_kind == ibQueryAstExprKind::Column && e->m_path.size() == 1
			? std::find_if(ast.m_projections.begin(), ast.m_projections.end(),
				[&e](const ibQueryProjection& p) { return ibQueryOutputName(p).IsSameAs(e->m_path.front(), false); })
			: ast.m_projections.end();
		refuse(output != ast.m_projections.end() ? output->m_expr : e, e);
		walk(e);
	}
}

// ⭐⭐ THE LINKS MUST NOT CONTRADICT ONE ANOTHER, and it is the ENGINE that says so.
//
// A join condition is a sentence about TWO tables. Two ways of writing one are not links at all, and
// both are easy to write by hand and impossible to see afterwards:
//
//   * a table joined to ITSELF — every column of the condition roots in the same source. Whatever it
//     filters, it says nothing about how the two tables meet, and the join is left unconstrained.
//   * a condition that never MENTIONS the table it is written on. That is a filter wearing a join's
//     clothes: for an INNER join it happens to behave like one in WHERE, for an OUTER join it does
//     NOT (it pre-filters the null-padded side), so the same text means two different things
//     depending on a box ticked elsewhere. The language has a place for a filter, and it is WHERE.
//
// Said here rather than in the constructor's grid because it is a fact about the QUERY, not about
// the window: the same text typed into the query editor deserves the same answer.
void CheckJoinsAreConsistent(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources)
{
	// WHICH source a column path stands on, BY POSITION — because position is what one of the rules
	// below is about. -1 when nothing answers: that is an UNKNOWN COLUMN, which the ordinary name
	// check reports in its own words, so this one stays quiet about it.
	const auto rootOf = [&sources](const ibQueryAstExpr& e) -> int {
		if (e.m_path.empty())
			return -1;
		if (e.m_path.size() >= 2)
			for (size_t i = 0; i < sources.size(); ++i)
				if (!sources[i].m_alias.IsEmpty() && sources[i].m_alias.IsSameAs(e.m_path[0], false))
					return static_cast<int>(i);
		for (size_t i = 0; i < sources.size(); ++i)
			if (sources[i].m_q != nullptr && sources[i].m_q->ResolveColumnByName(e.m_path[0]) != nullptr)
				return static_cast<int>(i);
		return -1;
	};

	for (size_t i = 0; i < ast.m_joins.size(); ++i) {
		const ibQueryAstJoin& join = ast.m_joins[i];
		// ⚠ NO LINK IS NOT A MISTAKE, AND NOTHING HERE IS CHECKED ABOUT IT. A link either exists or it
		// does not; where it does not, the two tables are multiplied, and that is a complete, ordinary
		// query. This check is about links that CONTRADICT one another — where there is none, there is
		// nothing to contradict.
		//
		// (It refused one for a while, on the reading that a bare JOIN looks unfinished. It does not:
		// adding two tables and linking neither is the first thing anybody does, and the red line
		// under the tabs said the query was broken while it was merely young.)
		//
		// ⚠ AND THERE IS NO EXCEPTION FOR THE KIND EITHER. An outer join with no condition was refused
		// here for one build, on the reading that "all rows of X" is a sentence about a link. It is —
		// but a KIND left behind on a table whose link was deleted is not the author saying anything,
		// it is a leftover, and the red line then appeared over a Links tab with no rows in it at all.
		// The leftover is fixed where it is made (the kind goes back to inner with the link); nothing
		// is checked about a condition nobody wrote.
		if (!join.m_on)
			continue;
		// `ON TRUE` is the product said out loud — the same thing, so the same silence.
		if (join.m_on->m_kind == ibQueryAstExprKind::Literal)
			continue;

		// The table this link is written ON: sources[0] is the FROM, joins[k] is sources[k + 1].
		const int bound = static_cast<int>(i) + 1;
		if (bound >= static_cast<int>(sources.size()))
			continue;

		std::vector<ibQueryAstExprPtr> columns;
		CollectColumns(join.m_on, columns);

		bool mentionsJoined = false;
		bool mentionsOther  = false;
		for (const ibQueryAstExprPtr& column : columns) {
			const int root = column != nullptr ? rootOf(*column) : -1;
			if (root < 0)
				continue;   // unknown column — the name check owns that complaint

			// ⭐⭐ AND IT MAY NAME MORE THAN THE TWO SIDES — but only tables the query has ALREADY
			// read. A link over three tables is an ordinary thing to write (`a.x = b.y AND a.z =
			// c.w`): the sources are joined left to right, so everything before this one is standing
			// there to be compared against.
			//
			// What cannot work is naming a table that comes AFTER: at the moment this join is made
			// that table has not been read, and there is nothing to compare with. The reader cannot
			// see the difference — both look like a table of the same query — so the ORDER is what
			// makes two tables compatible here, and the engine is the one that knows it.
			if (root > bound)
				ThrowQueryException(join.m_on->m_line, join.m_on->m_col, ibString::Format(
					_("the link on '%s' refers to '%s', which this query reads AFTER it: a link can "
					  "only relate tables that are already read - move the table earlier, or write "
					  "this as a condition"),
					ibQuerySourceName(join.m_source),
					ibQuerySourceName(ast.m_joins[static_cast<size_t>(root) - 1].m_source)));

			if (root == bound) mentionsJoined = true;
			else               mentionsOther  = true;
		}

		if (!mentionsJoined && !mentionsOther)
			continue;   // nothing resolved — say nothing rather than guess

		if (!mentionsJoined)
			ThrowQueryException(join.m_on->m_line, join.m_on->m_col, ibString::Format(
				_("this link says nothing about '%s': a condition that does not relate the joined "
				  "table is a filter, and belongs in the conditions"),
				ibQuerySourceName(join.m_source)));

		if (!mentionsOther)
			ThrowQueryException(join.m_on->m_line, join.m_on->m_col, ibString::Format(
				_("'%s' is linked to itself: a link relates TWO tables, so each side has to stand "
				  "on a different one"),
				ibQuerySourceName(join.m_source)));
	}
}

// ⭐⭐ AN AGGREGATE NAMES A FIELD OF THE SELECTION — the rule, in ONE place, because TWO doors ask it.
// `TOTALS COUNT(Number)` over `SELECT Posted` would put a column called `Number` into a result whose
// author never asked for one (Max, 2026-08-22, seeing it appear: "wrong, I have no Number in the
// selection"). A LEVEL may group by anything: it adds depth and no column. An aggregate WRITES a
// column, and a column of the result has to have been declared by the SELECT that declares it.
//
// Living here rather than inside the lowering is the whole point: the constructor's verdict line
// resolves names through CheckNames and the run goes through ExecuteTotals, and a rule written into
// only one of them is a rule the other says nothing about — which is exactly what happened, the
// dialog cheerfully reporting "the query engine reads this query" about a query it does not.
//
// A selected field has TWO spellings and both are it: the alias (`Parent`) and the way the SELECT
// wrote it (`Catalog1.Parent`), which is what the old constructor put in these lines. So the test is
// "does some projection claim this path", by output name or by prefix. A dot-walk CONTINUES from a
// claimed field: `Parent.Description` is claimed by `SELECT Parent`, the walk starting inside the
// result. COUNT(*) names nothing and is always in.
// ⭐⭐ `OVER <name>` NAMES A GROUPING OF THIS VERY QUERY — checked HERE, where the window verifies a
// query, and not only when it runs. A misspelt area is the easiest mistake to make in the totals
// grid (a name typed by hand, or one that survived a grouping being renamed), and finding it at
// composition time means finding it in front of a report that shows nothing.
//
// The names are the ones the lowering resolves by: a level's alias, else its head field's name,
// qualified by the separator for the levels that sit on one.
void CheckTotalsScopeNames(const ibQuerySelect& ast)
{
	std::vector<ibString> known;
	const auto nameOf = [](const ibQueryTotalDim& level) -> ibString {
		if (!level.m_alias.IsEmpty())
			return level.m_alias;
		const ibQueryTotalField* head = level.Head();
		return head != nullptr && head->m_expr != nullptr && !head->m_expr->m_path.empty()
			? head->m_expr->m_path.back() : ibString();
	};
	for (const ibQueryTotalDim& level : ast.m_totalsBy)
		if (const ibString name = nameOf(level); !name.IsEmpty())
			known.push_back(name);
	for (const ibQueryTotalSplit& node : ast.m_totalsSplits)
		for (const ibQueryTotalDim& level : node.m_levels) {
			const ibString name = nameOf(level);
			if (name.IsEmpty())
				continue;
			known.push_back(name);
			if (!node.m_name.IsEmpty())
				known.push_back(node.m_name + ibT(".") + name);
		}

	for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates) {
		if (resource.m_scope.IsEmpty())
			continue;
		ibStringTokenizer names(resource.m_scope, ibT(","));
		while (names.HasMoreTokens()) {
			ibString one = names.GetNextToken();
			one.Trim(true).Trim(false);
			if (one.IsEmpty())
				continue;
			bool found = false;
			for (const ibString& had : known)
				if (had.IsSameAs(one, false)) { found = true; break; }
			if (!found)
				ThrowQueryException(0, 0,
					_("OVER \"%s\": this query groups by nothing of that name: name one of its own groupings, qualified by the separator (Separator.Grouping) where it sits on one"),
					one);
		}
	}
}

void CheckTotalsNameSelectedFields(const ibQuerySelect& ast)
{
	if (ast.m_totalsAggregates.empty() || ast.m_selectAll)
		return;

	std::map<ibString, const ibQueryProjection*, ibNoCaseLess> byOutputName;
	{
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr) continue;
			byOutputName[OutputNameFor(ast, p, idx++)] = &p;
		}
	}

	auto claimed = [&ast, &byOutputName](const ibQueryAstExprPtr& expr) -> bool {
		if (!expr)
			return false;
		if (expr->m_kind != ibQueryAstExprKind::Column)
			return true;                                    // an expression stands for itself, not a field
		if (expr->m_path.empty())
			return false;
		if (byOutputName.find(expr->m_path.front()) != byOutputName.end())
			return true;                                    // by the name the result calls it
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star)
				return true;
			if (!p.m_expr || p.m_expr->m_kind != ibQueryAstExprKind::Column || p.m_expr->m_path.empty())
				continue;
			if (p.m_expr->m_path.size() > expr->m_path.size())
				continue;
			if (std::equal(p.m_expr->m_path.begin(), p.m_expr->m_path.end(), expr->m_path.begin(),
			               [](const ibString& a, const ibString& b) { return a.IsSameAs(b, false); }))
				return true;                                // spelled the way the SELECT spelled it
		}
		return false;
	};

	// Every call of a figure — the figure itself, or each one a figure made of figures holds (`SUM(a) - SUM(b)`).
	const std::function<void(const ibQueryAstExprPtr&)> check = [&](const ibQueryAstExprPtr& agg) {
		if (!agg)
			return;
		if (agg->m_kind != ibQueryAstExprKind::Func) {
			ibQueryForEachChild(*agg, check);
			return;
		}
		if (agg->m_star || !agg->m_arg)
			return;
		if (!claimed(agg->m_arg))
			ThrowQueryException(agg->m_arg->m_line, agg->m_arg->m_col,
				_("TOTALS aggregates \"%s\", which the selection does not carry: add it to SELECT and total over it there"),
				ibRenderQueryExpr(*agg->m_arg));
	};
	for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates)
		check(resource.m_expr);
}

// ⚠ INDEX BY NAMES THE TABLE THIS STATEMENT MAKES — its output fields, by the names the run gives them (OutputNameFor)
// and keys the index by (the last segment written). A name that is none of them built no index and said nothing: the
// statements after it read the table whole, and a typo cost exactly the speed it was written for. Said by the check
// (CheckSelectNames) and by the run that stores the table, as CheckTotalsNameSelectedFields is.
void CheckIndexNamesOutputs(const ibQuerySelect& ast)
{
	if (ast.m_indexBy.empty() || ast.m_selectAll)
		return;

	std::vector<ibString> outputs;
	int idx = 0;
	for (const ibQueryProjection& p : ast.m_projections) {
		if (p.m_star)
			return;                                         // its fields are a source's: nothing here to judge by
		if (p.m_expr)
			outputs.push_back(OutputNameFor(ast, p, idx++));
	}

	for (const ibQueryAstExprPtr& column : ast.m_indexBy) {
		if (!column || column->m_path.empty())
			continue;
		const ibString& name = column->m_path.back();
		if (std::none_of(outputs.begin(), outputs.end(),
				[&name](const ibString& output) { return output.IsSameAs(name, false); }))
			ThrowQueryException(column->m_line, column->m_col,
				_("'%s' is not a field of the table this statement makes: INDEX BY names its output fields"), name);
	}
}

void CheckSelectNames(const ibQuerySelect& astAsWritten, const std::map<ibString, ibValue>& params)
{
	// ⭐ JUDGE WHAT WILL RUN, NOT WHAT WAS TYPED. The execution reorders a run of INNER joins so a
	// link may name a table added after it (ibQueryRewrite::ReorderJoins — an OUTER join never moves,
	// a cycle is left alone). Checking the text in its written order would report a forward reference
	// the engine never hits, which is the worst kind of complaint: true about the sentence, false
	// about the query, and impossible for the author to act on.
	//
	// Only that one rule is applied. The rest of the optimizer rephrases the query, and a check must
	// not judge a rephrasing the author cannot see.
	const ibQuerySelectPtr ordered = astAsWritten.m_joins.size() > 1
		? ibQueryRewrite::ReorderJoins(astAsWritten) : nullptr;
	const ibQuerySelect& ast = ordered ? *ordered : astAsWritten;

	// A nested SELECT anywhere in an expression is checked with ITS own sources.
	auto checkNested = [&params](const ibQueryAstExprPtr& e) {
		if (!e) return;
		if (e->m_subquery) CheckSelectNames(*e->m_subquery, params);
	};

	// ⭐ A LATER BRANCH IS JUDGED UNDER THE FIRST BRANCH'S NAMES, position by position — which is how the
	// execution builds it (BuildUnionStack). Judged under its own, `SELECT … AS Month, … AS ForMonth …
	// UNION ALL SELECT D.RegistrationPeriod, …, D.RegistrationPeriod …` was refused for two outputs called
	// RegistrationPeriod: names the union never uses (the payroll demo, 2026-09-10).
	for (const ibQuerySelectPtr& branch : ast.m_unions) {
		if (!branch)
			continue;
		ibQuerySelect named = *branch;
		const bool listed = !named.m_selectAll && named.m_projections.size() == ast.m_projections.size()
			&& std::none_of(named.m_projections.begin(), named.m_projections.end(),
				[](const ibQueryProjection& p) { return p.m_star; });
		if (listed)
			for (size_t i = 0; i < named.m_projections.size(); ++i)
				named.m_projections[i].m_alias = ibQueryOutputName(ast.m_projections[i]);
		CheckSelectNames(named, params);
	}

	// ⭐ A CAST'S TYPE IS A NAME TOO — a primitive, or a table a reference points at — and one that is neither is
	// said here, where the text is judged, in the words the run would use (CastTargetType). It needs no source,
	// so it is asked before the check may fall silent over tables it cannot see: a temporary table described by
	// CASTs reads a table handed in, which is exactly such a source. So is a VALUE's — `VALUE(Enum.Kind.Retail)` names a
	// metaobject and its member, and is settled through the door every constant passes (EvalValue): one that is not
	// there went through the check and was refused only when the query ran (2026-11-11).
	std::function<void(const ibQueryAstExprPtr&)> checkSourcelessNames = [&](const ibQueryAstExprPtr& e) {
		if (!e)
			return;
		if (e->m_kind == ibQueryAstExprKind::Cast)
			CastTargetType(*e);
		else if (e->m_kind == ibQueryAstExprKind::Value)
			EvalValue(*e, params);
		ibQueryForEachChild(*e, [&checkSourcelessNames](const ibQueryAstExprPtr& child) { checkSourcelessNames(child); });
	};
	for (const ibQueryProjection& projection : ast.m_projections)
		checkSourcelessNames(projection.m_expr);
	checkSourcelessNames(ast.m_where);
	checkSourcelessNames(ast.m_having);
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		checkSourcelessNames(key);
	for (const ibQueryAstJoin& join : ast.m_joins)
		checkSourcelessNames(join.m_on);
	for (const ibQueryOrderItem& item : ast.m_orderBy)
		checkSourcelessNames(item.m_expr);
	// …and INDEX BY, asked of the outputs alone, so a statement over a table handed in is judged too.
	CheckIndexNamesOutputs(ast);

	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/true, /*tolerateOpaque=*/true)) {
		// ⚠ SILENCE IS FOR "CANNOT VERIFY", NOT FOR "NOTHING TO VERIFY AGAINST".
		//
		// A one-segment name may be a temp table made by a statement this check cannot see, so a
		// query whose sources did not resolve is left alone rather than accused. But a query with NO
		// TABLE AT ALL that still selects `SliceLast.Dimension2` is not unverifiable — it is plainly
		// wrong, and every one of its qualified fields is a leftover from a table that is gone.
		//
		// It passed silently, and the verdict line said the engine reads it. That is the one sentence
		// this window must never say untruthfully.
		const bool noTables = ast.m_joins.empty()
			&& ast.m_from.m_name.empty() && !ast.m_from.m_subquery;
		if (noTables) {
			std::vector<ibQueryAstExprPtr> columns;
			for (const ibQueryProjection& projection : ast.m_projections)
				CollectColumns(projection.m_expr, columns);
			for (const ibQueryAstExprPtr& column : columns)
				if (column != nullptr && column->m_path.size() >= 2) {
					ibString field;
					for (size_t k = 0; k < column->m_path.size(); ++k)
						field += (k > 0 ? ibT(".") : ibT("")) + column->m_path[k];
					ThrowQueryException(column->m_line, column->m_col, ibString::Format(
						_("this query reads no table, yet '%s' is selected from one: remove the field, "
						  "or add '%s' to the tables"), field, column->m_path[0]));
				}
		}
		return;   // unverifiable — silence, never a false error
	}

	CheckJoinsAreConsistent(ast, sources);
	CheckUnlimitedStringsAreCast(ast, sources);

	// TWO LISTS, because one clause may legitimately name something the tables do not have.
	//
	// ⚠ THE EXEMPTION BELONGS TO `ORDER BY` ALONE. `ORDER BY Total` over `SUM(x) AS Total` names an
	// OUTPUT, and the executing path resolves it against the projection list. Applying that leniency
	// to every column was a hole big enough to drive the whole check through: a projection column IS
	// its own output name, so `SELECT ReferenceReference` declared itself known and walked straight
	// past the resolver. The exemption has to be as narrow as the rule that earns it.
	std::vector<ibQueryAstExprPtr> strict;    // must name a real column of a real source
	std::vector<ibQueryAstExprPtr> ordering;  // may instead name an output of this select

	for (const ibQueryProjection& projection : ast.m_projections) {
		CollectColumns(projection.m_expr, strict);
		checkNested(projection.m_expr);
	}
	CollectColumns(ast.m_where, strict);
	checkNested(ast.m_where);
	CollectColumns(ast.m_having, strict);
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		CollectColumns(key, strict);
	for (const ibQueryTotalAggregate& aggregate : ast.m_totalsAggregates)
		CollectColumns(aggregate.m_expr, strict);
	// …and the aggregates answer one question the resolver cannot: whether the RESULT has a column
	// for the figure. Asked here so the dialog's verdict and the run agree.
	//
	// ⚠ ON THE FULLY REWRITTEN FORM, and this one rule is the exception to the paragraph at the top
	// of this function. Everywhere else a check judges what was WRITTEN, because complaining about a
	// rephrasing the author cannot see is useless to them. Here the rephrasing is what makes the
	// query legal: flattening a nested SELECT substitutes its aliases, so
	// `SELECT Q.a AS x FROM (SELECT b AS a FROM …) AS Q TOTALS SUM(a) BY x` names nothing the outer
	// text carries and everything the flattened query does. Judged as typed, the dialog refuses a
	// query the engine runs — the same false complaint, arrived at from the other side.
	const ibQuerySelectPtr asRun = ibQueryRewrite::Rewrite(astAsWritten);
	CheckTotalsNameSelectedFields(asRun ? *asRun : ast);
	CheckTotalsScopeNames(ast);   // …and that every OVER names a grouping this query declares
	for (const ibQueryTotalDim& dim : ast.m_totalsBy)
		for (const ibQueryTotalField& field : dim.m_fields)
			CollectColumns(field.m_expr, strict);
	for (const ibQueryAstJoin& join : ast.m_joins)
		CollectColumns(join.m_on, strict);
	for (const ibQueryOrderItem& item : ast.m_orderBy)
		CollectColumns(item.m_expr, ordering);
	// THE SAME RESOLVER THE EXECUTION USES. Its words are the ones the user sees — there is no
	// second definition here of what a known field is. A column of a source this check cannot see into
	// (a nested select) is left alone by the resolver itself (ResolvePath), for this loop and every
	// question below alike.
	for (const ibQueryAstExprPtr& column : strict) {
		if (column == nullptr || column->m_path.empty())
			continue;
		ResolvePath(sources, *column);   // raises "unknown attribute" / "ambiguous attribute"
	}

	// ⚠ ASKED, NOT RESPELLED. This is the gate that refuses duplicate output names, so the name it
	// compares MUST be the name everything else uses — a second spelling here would refuse queries
	// the constructor generates, or pass ones it should refuse, and only when the two drifted.
	std::vector<ibString> outputs;
	for (const ibQueryProjection& projection : ast.m_projections) {
		const ibString name = ibQueryOutputName(projection);
		if (!name.IsEmpty())
			outputs.push_back(name);
	}

	// ⚠ TWO OUTPUT FIELDS CANNOT SHARE A NAME, and in THIS engine that is not a style rule. The
	// output is read back BY NAME: a temp table's columns are these names and its index is built over
	// them, and a totals level answers to one. (A union's later branches take the first one's names by
	// position — see the branch loop above — so it is the first branch's names that must be unique.) Two columns
	// called the same thing make every one of those ambiguous, and the ambiguity is silent — the
	// query runs and one of them wins.
	//
	// Checked HERE, in the engine, so the rule holds for hand-written text as much as for what the
	// constructor produces — and so the constructor does not have to own a rule of its own.
	for (size_t i = 0; i < outputs.size(); ++i)
		for (size_t k = i + 1; k < outputs.size(); ++k)
			if (!outputs[i].IsEmpty() && outputs[i].IsSameAs(outputs[k], false))
				ThrowQueryException(0, 0, ibString::Format(
					_("two output fields are called '%s': give one of them a different alias"), outputs[i]));

	for (const ibQueryAstExprPtr& column : ordering) {
		if (column == nullptr || column->m_path.empty())
			continue;
		bool namesAnOutput = false;
		if (column->m_path.size() == 1)
			for (const ibString& name : outputs)
				if (name.IsSameAs(column->m_path[0], false)) { namesAnOutput = true; break; }
		if (!namesAnOutput)
			ResolvePath(sources, *column);
	}

	// ⚠ AN AGGREGATE OVER A TYPE IT CANNOT FOLD IS REFUSED — `SUM(Description)`, `AVG(Supplier)`.
	//
	// Asked here rather than at execution because the answer never depends on the DATA: it follows
	// from the column's declared TYPE, which is known the moment the query is written. Left to the
	// run, it surfaces as whatever the driver makes of adding strings together — a dialect-specific
	// error at best, zeroes at worst, and either way in front of a user rather than an author.
	//
	// Only what the fold genuinely means is allowed:
	//   SUM / AVG  — NUMBER alone. There is no sum of dates and no average of references.
	//   MIN / MAX  — anything ORDERED: number, date, string. A reference has no order of its own
	//                (it keys by guid), so folding one would rank rows by an internal identity.
	//   COUNT      — anything at all. Counting asks nothing of the type.
	//
	// A COMPOSITE column (several clsids) is not refused: which type a row holds is a fact about the
	// row, and refusing on "it might be a string" would be this check inventing an answer. That is
	// the same promise the rest of the check makes — silence where it cannot verify.
	for (const ibQueryProjection& projection : ast.m_projections) {
		const ibQueryAstExprPtr& expr = projection.m_expr;
		if (!expr || expr->m_kind != ibQueryAstExprKind::Func || expr->m_star || !expr->m_arg)
			continue;
		if (expr->m_func == ibQueryKeyword::Count)
			continue;   // counting asks nothing of the type
		if (expr->m_arg->m_kind != ibQueryAstExprKind::Column || expr->m_arg->m_path.empty())
			continue;   // a computed argument is checked by what it is made of, not here

		const std::vector<std::shared_ptr<const ibBackendQueryColumn>> argCols = ResolvePath(sources, *expr->m_arg);
		if (argCols.empty() || argCols.back() == nullptr)
			continue;
		// ⚠ THE SAME LIST THE CONSTRUCTOR OFFERS. Read here as a refusal, read there as what to put
		// in the dropdown — one answer, so a person is never shown a choice their engine rejects.
		const std::vector<ibQueryKeyword> allowed =
			ibQueryAggregatesFor(argCols.back()->GetTypeDesc());
		if (std::find(allowed.begin(), allowed.end(), expr->m_func) == allowed.end()) {
			const bool sumLike = expr->m_func == ibQueryKeyword::Sum || expr->m_func == ibQueryKeyword::Avg;
			ThrowQueryException(expr->m_line, expr->m_col, ibString::Format(
				_("%s cannot be taken over '%s': the field is not %s"),
				ibQueryKeywordText(expr->m_func), expr->m_arg->m_path.back(),
				sumLike ? _("a number") : _("an ordered value")));
		}
	}

	// ⚠⚠ A FOLDED COLUMN IS NOT ALSO A GROUP KEY. Grouping by the very column an aggregate folds
	// leaves one row per group, so the fold returns the value itself — writable, and never meant.
	// It is refused here rather than left to arrive as a puzzling result set.
	// ⚠ EACH SIDE RESOLVED ONCE. Resolving is a dot-walk through the sources, not a comparison, so
	// doing it inside the pairing loop cost (folded × keys) walks of the same two paths — and this
	// runs on every check, which is every keystroke in the constructor.
	{
		const std::vector<ibQueryAstExprPtr> foldedList = ibQueryAggregatedColumns(ast);
		std::vector<std::vector<std::shared_ptr<const ibBackendQueryColumn>>> keyCols;
		keyCols.reserve(ast.m_groupBy.size());
		for (const ibQueryAstExprPtr& key : ast.m_groupBy)
			keyCols.push_back(key && key->m_kind == ibQueryAstExprKind::Column && !key->m_path.empty()
				? ResolvePath(sources, *key) : std::vector<std::shared_ptr<const ibBackendQueryColumn>>());

		std::vector<std::vector<std::shared_ptr<const ibBackendQueryColumn>>> plainCols;
		for (const ibQueryProjection& projection : ast.m_projections)
			if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column
			    && !projection.m_expr->m_path.empty())
				plainCols.push_back(ResolvePath(sources, *projection.m_expr));

		for (const ibQueryAstExprPtr& folded : foldedList) {
			const std::vector<std::shared_ptr<const ibBackendQueryColumn>> foldedCols = ResolvePath(sources, *folded);

			// ⚠ UNLESS THE QUERY ALSO SELECTS IT PLAINLY. `SELECT Qty, SUM(Qty) … GROUP BY Qty` reads
			// oddly but means something, and the completeness rule right below DEMANDS that key — so
			// refusing it here would be the engine contradicting itself, one paragraph apart. What is
			// refused is the narrow case: a column folded and NOWHERE selected on its own, standing in
			// GROUP BY for no reason but a window that put it there.
			if (std::find(plainCols.begin(), plainCols.end(), foldedCols) != plainCols.end())
				continue;

			for (size_t i = 0; i < ast.m_groupBy.size(); ++i) {
				// SAME COLUMN, whichever way it is written — the resolved leaf, as everywhere else.
				if (keyCols[i].empty() || keyCols[i] != foldedCols)
					continue;
				ThrowQueryException(ast.m_groupBy[i]->m_line, ast.m_groupBy[i]->m_col, ibString::Format(
					_("'%s' is already aggregated, so it cannot also be a grouping field"),
					ast.m_groupBy[i]->m_path.back()));
			}
		}
	}

	RefuseUngrouped(ast, sources);
}


// Does this expression still resolve against these sources? Asked of the SAME resolver the
// execution uses, so "still there" means what it means everywhere else.
bool StillResolves(const std::vector<ibSourceBinding>& sources, const ibQueryAstExprPtr& e)
{
	std::vector<ibQueryAstExprPtr> columns;
	CollectColumns(e, columns);
	for (const ibQueryAstExprPtr& column : columns) {
		if (column == nullptr || column->m_path.empty())
			continue;
		try { ResolvePath(sources, *column); }
		catch (const ibCoreException&) { return false; }
	}
	return true;
}

int PruneSelect(ibQuerySelect& ast, const std::map<ibString, ibValue>& params)
{
	int dropped = 0;
	for (const ibQuerySelectPtr& branch : ast.m_unions)
		if (branch) dropped += PruneSelect(*branch, params);

	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/false))
		return dropped;   // unverifiable — leave it whole

	auto gone = [&](bool resolves) { if (!resolves) ++dropped; return !resolves; };

	std::erase_if(ast.m_projections,
		[&](const ibQueryProjection& p) { return gone(StillResolves(sources, p.m_expr)); });

	std::erase_if(ast.m_groupBy,
		[&](const ibQueryAstExprPtr& e) { return gone(StillResolves(sources, e)); });

	std::erase_if(ast.m_orderBy,
		[&](const ibQueryOrderItem& o) { return gone(StillResolves(sources, SortTargetOf(ast, o.m_expr))); });

	std::erase_if(ast.m_indexBy,
		[&](const ibQueryAstExprPtr& e) { return gone(StillResolves(sources, e)); });

	std::erase_if(ast.m_totalsAggregates,
		[&](const ibQueryTotalAggregate& r) { return gone(StillResolves(sources, r.m_expr)); });

	// A LEVEL LOSES THE FIELDS THAT NO LONGER RESOLVE, and only then, having none left, loses itself.
	// Dropping the whole level because one of its fields died would take the grouping away over a
	// field the author never touched.
	for (ibQueryTotalDim& d : ast.m_totalsBy)
		std::erase_if(d.m_fields,
			[&](const ibQueryTotalField& f) { return gone(StillResolves(sources, f.m_expr)); });

	std::erase_if(ast.m_totalsBy,
		[](const ibQueryTotalDim& d) { return d.m_fields.empty(); });

	// ⚠ THE NODES ARE PRUNED THE SAME WAY — a level whose field stopped resolving goes, and a node
	// left with nothing goes with it. Skipped here, a removed attribute would live on inside a
	// `SPLIT` and take the whole query down on the next run, which is exactly what this pass exists
	// to prevent.
	for (ibQueryTotalSplit& node : ast.m_totalsSplits) {
		for (ibQueryTotalDim& d : node.m_levels)
			std::erase_if(d.m_fields,
				[&](const ibQueryTotalField& f) { return gone(StillResolves(sources, f.m_expr)); });
		std::erase_if(node.m_levels,
			[](const ibQueryTotalDim& d) { return d.m_fields.empty(); });
	}

	bool nodesHoldLevels = false;
	for (const ibQueryTotalSplit& node : ast.m_totalsSplits)
		if (!node.m_levels.empty()) { nodesHoldLevels = true; break; }

	if (ast.m_totalsAggregates.empty() && ast.m_totalsBy.empty() && !nodesHoldLevels && !ast.m_totalsOverall)
		ast.m_hasTotals = false;   // TOTALS with nothing in it is not a TOTALS

	// A JOIN's condition goes, not the join: the join IS a source, and an empty ON means "join by
	// the reference between the tables" — a definition, not a hole.
	for (ibQueryAstJoin& join : ast.m_joins)
		if (join.m_on && !StillResolves(sources, join.m_on)) { join.m_on = nullptr; ++dropped; }

	// THE WHERE IS A CHAIN, so only the links that broke go — not the whole filter.
	if (ast.m_where) {
		std::vector<ibQueryAstExprPtr> rows;
		ibQueryFlattenAnd(ast.m_where, rows);
		const size_t before = rows.size();
		std::erase_if(rows,
			[&](const ibQueryAstExprPtr& e) { return !StillResolves(sources, e); });
		dropped += static_cast<int>(before - rows.size());
		ast.m_where = ibQueryFoldAnd(rows);
	}

	if (ast.m_projections.empty())
		ast.m_selectAll = true;   // a query with no named fields reads every one of them

	return dropped;
}

} // namespace ibLowering

int ibQueryLowering::PruneUnresolved(ibQueryPackage& package, const std::map<ibString, ibValue>& params)
{
	const ibCastLeafScope castLeaves;   // one query's narrowed fields (ibCastLeafScope) — at every door
	int dropped = 0;
	for (ibQueryAstStatement& statement : package.m_statements)
		if (statement.m_select)
			dropped += PruneSelect(*statement.m_select, params);
	return dropped;
}

void ibQueryLowering::CheckNames(const ibQueryPackage& package, const std::map<ibString, ibValue>& set)
{
	const ibCastLeafScope castLeaves;   // one query's narrowed fields (ibCastLeafScope) — at every door
	const std::map<ibString, ibValue> params = WithEveryParameter(ibRenderQueryPackage(package), set);
	for (const ibQueryAstStatement& statement : package.m_statements) {
		if (statement.m_select)
			CheckSelectNames(*statement.m_select, params);
		// A LOCK names its tables, their links and its key as a select's FROM and WHERE do — and is checked as one.
		else if (statement.IsLock()) {
			ibQuerySelect tables;
			tables.m_from      = statement.m_lock->m_select->m_from;
			tables.m_joins     = statement.m_lock->m_select->m_joins;
			tables.m_where     = statement.m_lock->m_select->m_where;
			tables.m_selectAll = true;
			CheckSelectNames(tables, params);
		}
	}
}

std::vector<ibQueryAstExprPtr> ibQueryLowering::UngroupedProjections(
	const ibQuerySelect& ast, const std::map<ibString, ibValue>& set)
{
	const ibCastLeafScope castLeaves;   // one query's narrowed fields (ibCastLeafScope) — at every door
	const std::map<ibString, ibValue> params = WithEveryParameter(ibRenderQuery(ast), set);   // describing — see WithEveryParameter
	// UNVERIFIABLE IS EMPTY, not a guess — the same promise CheckNames and PruneUnresolved make. A
	// host reading this as "the work still to do" would otherwise add group keys to a query nobody
	// could resolve.
	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/false))
		return {};

	try {
		return CollectUngrouped(ast, sources);
	}
	catch (const ibCoreException&) {
		// A name that does not resolve is CheckNames' verdict to give, with its own words. Here it
		// only means "nothing can be said about grouping yet".
		return {};
	}
}
