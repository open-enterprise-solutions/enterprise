////////////////////////////////////////////////////////////////////////////
//	L4 — optimizer rewrite pass: negation normalization + FROM-subquery
//	flattening (queryRewrite.h). Pure AST -> AST; runs on a deep clone.
////////////////////////////////////////////////////////////////////////////

#include "queryRewrite.h"
#include "queryRender.h"   // ibQueryOutputName - the one answer to "what is this column called"

#include <functional>
#include <map>

namespace {

ibQuerySelectPtr CloneSelect(const ibQuerySelect& s);

// Deep-clone an expression tree (the shallow struct copy shares children via
// shared_ptr — every child slot is re-cloned so the rewrite may mutate freely).
ibQueryAstExprPtr CloneExpr(const ibQueryAstExprPtr& e)
{
	if (!e) return nullptr;
	auto c = std::make_shared<ibQueryAstExpr>(*e);
	c->m_arg  = CloneExpr(e->m_arg);
	c->m_lhs  = CloneExpr(e->m_lhs);
	c->m_rhs  = CloneExpr(e->m_rhs);
	c->m_low  = CloneExpr(e->m_low);
	c->m_high = CloneExpr(e->m_high);
	c->m_else = CloneExpr(e->m_else);
	c->m_list.clear();
	for (const ibQueryAstExprPtr& i : e->m_list)
		c->m_list.push_back(CloneExpr(i));
	c->m_cases.clear();
	for (const auto& wt : e->m_cases)
		c->m_cases.emplace_back(CloneExpr(wt.first), CloneExpr(wt.second));
	if (e->m_subquery)
		c->m_subquery = CloneSelect(*e->m_subquery);
	return c;
}

void CloneSourceInPlace(ibQuerySource& src)
{
	if (src.m_subquery)
		src.m_subquery = CloneSelect(*src.m_subquery);
	for (ibQueryAstExprPtr& a : src.m_args)
		a = CloneExpr(a);
}

ibQuerySelectPtr CloneSelect(const ibQuerySelect& s)
{
	auto c = std::make_shared<ibQuerySelect>(s);
	for (ibQueryProjection& p : c->m_projections)
		p.m_expr = CloneExpr(p.m_expr);
	CloneSourceInPlace(c->m_from);
	for (ibQueryAstJoin& j : c->m_joins) {
		CloneSourceInPlace(j.m_source);
		j.m_on = CloneExpr(j.m_on);
	}
	c->m_where  = CloneExpr(c->m_where);
	for (ibQueryAstExprPtr& g : c->m_groupBy)
		g = CloneExpr(g);
	c->m_having = CloneExpr(c->m_having);
	for (ibQueryAstExprPtr& index : c->m_indexBy)
		index = CloneExpr(index);
	for (ibQueryOrderItem& o : c->m_orderBy)
		o.m_expr = CloneExpr(o.m_expr);
	for (ibQueryAstExprPtr& t : c->m_totalsAggregates)
		t = CloneExpr(t);
	for (ibQueryTotalDim& d : c->m_totalsBy)
		for (ibQueryTotalField& f : d.m_fields)
			f.m_expr = CloneExpr(f.m_expr);
	for (std::shared_ptr<ibQuerySelect>& u : c->m_unions)
		u = CloneSelect(*u);
	return c;
}

//////////////////////////////////////////////////////////////////////
// Rule 1 — negation normalization
//////////////////////////////////////////////////////////////////////

ibQueryCompareOp InvertCompare(ibQueryCompareOp op)
{
	switch (op) {
	case ibQueryCompareOp::Eq: return ibQueryCompareOp::Ne;
	case ibQueryCompareOp::Ne: return ibQueryCompareOp::Eq;
	case ibQueryCompareOp::Lt: return ibQueryCompareOp::Ge;
	case ibQueryCompareOp::Ge: return ibQueryCompareOp::Lt;
	case ibQueryCompareOp::Le: return ibQueryCompareOp::Gt;
	case ibQueryCompareOp::Gt: return ibQueryCompareOp::Le;
	}
	return op;
}

// Push NOT down until absorbed. Returns the (possibly replaced) node; mutates the
// cloned tree in place. After this pass a WHERE coming out of the grammar carries
// no Not nodes at all — every form below absorbs it.
ibQueryAstExprPtr NormalizeNeg(const ibQueryAstExprPtr& e)
{
	if (!e) return nullptr;

	if (e->m_kind == ibQueryAstExprKind::Logical) {
		e->m_lhs = NormalizeNeg(e->m_lhs);
		e->m_rhs = NormalizeNeg(e->m_rhs);
		return e;
	}
	if (e->m_kind != ibQueryAstExprKind::Not)
		return e;

	const ibQueryAstExprPtr inner = e->m_lhs;
	switch (inner->m_kind) {
	case ibQueryAstExprKind::Not:                       // NOT NOT p -> p
		return NormalizeNeg(inner->m_lhs);

	case ibQueryAstExprKind::Logical: {                 // De Morgan, then recurse
		auto out = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		out->m_isOr = !inner->m_isOr;
		out->m_line = e->m_line; out->m_col = e->m_col;
		auto mkNot = [](const ibQueryAstExprPtr& c) {
			auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
			n->m_lhs = c; n->m_line = c->m_line; n->m_col = c->m_col;
			return n;
		};
		out->m_lhs = NormalizeNeg(mkNot(inner->m_lhs));
		out->m_rhs = NormalizeNeg(mkNot(inner->m_rhs));
		return out;
	}

	case ibQueryAstExprKind::Compare:                   // NOT (a < b) -> a >= b
		inner->m_cmp = InvertCompare(inner->m_cmp);
		return inner;

	case ibQueryAstExprKind::Like:                      // toggle the node's own negation
	case ibQueryAstExprKind::In:
	case ibQueryAstExprKind::IsNull:
	case ibQueryAstExprKind::Between:
		inner->m_negated = !inner->m_negated;
		return inner;

	case ibQueryAstExprKind::Column: {                  // NOT col (truthy) -> col = FALSE
		// Exact under OES semantics: an attribute holds a typed empty, never SQL NULL,
		// so a boolean column is always TRUE or FALSE (mirrors the truthy form col = TRUE).
		auto cmp = ibQueryAstExpr::Make(ibQueryAstExprKind::Compare);
		cmp->m_cmp = ibQueryCompareOp::Eq;
		cmp->m_lhs = inner;
		cmp->m_rhs = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		cmp->m_rhs->m_literal = ibValue(false);
		cmp->m_line = e->m_line; cmp->m_col = e->m_col;
		cmp->m_rhs->m_line = e->m_line; cmp->m_rhs->m_col = e->m_col;
		return cmp;
	}

	default:                                            // unknown inner — keep the Not,
		return e;                                       // the lowering reports it precisely
	}
}

//////////////////////////////////////////////////////////////////////
// Rule 2 — FROM-subquery flattening
//////////////////////////////////////////////////////////////////////

// Visit every Column node of an expression tree. Does NOT descend into a nested
// SELECT (IN (subquery)) — that is its own name scope.
void WalkColumns(const ibQueryAstExprPtr& e, const std::function<void(ibQueryAstExpr&)>& fn)
{
	if (!e) return;
	// ⚠ A COLUMN ROOTED ON A CAST IS NOT THIS SOURCE'S COLUMN. `CAST(x AS T).A` walks the fields of
	// T, so its path names nothing the flattening could substitute an inner projection for — mapping
	// `A` onto an output name of the subquery would rewrite it into a column of the wrong table.
	// The cast's own argument IS this source's, so the walk descends into it instead.
	if (e->m_kind == ibQueryAstExprKind::Column && e->m_arg
	    && e->m_arg->m_kind == ibQueryAstExprKind::Cast) {
		WalkColumns(e->m_arg->m_arg, fn);
		return;
	}
	if (e->m_kind == ibQueryAstExprKind::Column) { fn(*e); return; }
	WalkColumns(e->m_arg, fn);
	WalkColumns(e->m_lhs, fn);
	WalkColumns(e->m_rhs, fn);
	WalkColumns(e->m_low, fn);
	WalkColumns(e->m_high, fn);
	WalkColumns(e->m_else, fn);
	for (const ibQueryAstExprPtr& i : e->m_list)
		WalkColumns(i, fn);
	for (const auto& wt : e->m_cases) {
		WalkColumns(wt.first, fn);
		WalkColumns(wt.second, fn);
	}
}

// Visit every nested SELECT hanging off an expression tree (IN (subquery)).
void WalkInSubqueries(const ibQueryAstExprPtr& e, const std::function<void(ibQuerySelect&)>& fn)
{
	if (!e) return;
	if (e->m_subquery) fn(*e->m_subquery);
	WalkInSubqueries(e->m_arg, fn);
	WalkInSubqueries(e->m_lhs, fn);
	WalkInSubqueries(e->m_rhs, fn);
	WalkInSubqueries(e->m_low, fn);
	WalkInSubqueries(e->m_high, fn);
	WalkInSubqueries(e->m_else, fn);
	for (const ibQueryAstExprPtr& i : e->m_list)
		WalkInSubqueries(i, fn);
	for (const auto& wt : e->m_cases) {
		WalkInSubqueries(wt.first, fn);
		WalkInSubqueries(wt.second, fn);
	}
}

struct NoCaseLess
{
	bool operator()(const wxString& a, const wxString& b) const { return a.CmpNoCase(b) < 0; }
};

// The inner SELECT is mergeable when it is a pure projection over its source:
// no aggregates / grouping / having, no DISTINCT (distinct over a projection is
// not distinct over the outer's), no JOIN / UNION / TOTALS, no ORDER BY (kept
// conservative — the old wrapped path still serves those).
// ⭐ WHY A NESTED SELECT STAYED NESTED. The flattening is the FIRST of the two roads out of a nested
// source (the second is declaring it to the server as a CTE — queryLowering ResolveFrom), so what it
// refuses decides what the next tier is even asked. Said where the refusal is made, with what it is
// about; in Release the whole line is `((void)0)` and this is the `return false` it always was.
#define FlattenDecline(fmt, ...) \
	do { ibJournalInfo(wxT("query.rewrite"), wxT("nested FROM not flattened: ") fmt, ##__VA_ARGS__); \
	     return false; } while (false)

bool InnerIsFlattenable(const ibQuerySelect& inner)
{
	if (!inner.m_joins.empty())    FlattenDecline(wxT("the inner query has a JOIN"));
	if (!inner.m_unions.empty())   FlattenDecline(wxT("the inner query has a UNION"));
	if (inner.m_hasTotals)         FlattenDecline(wxT("the inner query has TOTALS"));
	if (inner.m_distinct)          FlattenDecline(wxT("the inner query is DISTINCT"));
	if (!inner.m_groupBy.empty())  FlattenDecline(wxT("the inner query has a GROUP BY"));
	if (inner.m_having)            FlattenDecline(wxT("the inner query has a HAVING"));
	// ⭐ A NESTED QUERY HAS NO ORDER, so an inner `ORDER BY` neither blocks the flattening nor
	// survives it (Max: "a nested query does not support order or totals — they are not there by
	// definition, and the constructor does not offer them either"). SQL agrees: nothing is promised
	// about the order of a derived table's rows. The one shape where it would decide WHICH rows
	// survive is TOP, and that is refused on the next line.
	//
	// This is what kept the whole read in RAM: an author's query carries an ORDER BY of its own, the
	// composition wrapped it as a nested source, and the wrapper then refused to collapse — so the
	// source stayed an ibSubqueryQueryable, which computes its rows in memory by construction, and no
	// totals push-down could ever fire above it.
	if (inner.m_top > 0)           FlattenDecline(wxT("the inner query has TOP, whose limit merging would lose"));
	// The statement words the inner may carry. Merging them away is the SILENT kind of wrong:
	// ALLOWED would become a refusal the author asked not to have, FOR UPDATE would stop holding
	// the rows, INTO would stop materialising anything — and the query would still run.
	if (inner.m_allowed)           FlattenDecline(wxT("the inner query is SELECT ALLOWED"));
	if (inner.m_forUpdate)         FlattenDecline(wxT("the inner query is FOR UPDATE"));
	if (!inner.m_intoTemp.IsEmpty()) FlattenDecline(wxT("the inner query has INTO"));
	if (inner.m_selectAll) return true;
	for (const ibQueryProjection& p : inner.m_projections) {
		if (p.m_star)  FlattenDecline(wxT("the inner query projects a qualified star"));
		if (!p.m_expr || p.m_expr->m_kind != ibQueryAstExprKind::Column)
			FlattenDecline(wxT("the inner query projects an expression, not a plain column"));
	}
	return true;
}

// FROM (SELECT … FROM X WHERE p) AS s  +  outer clauses  ->  FROM X, outer names
// substituted to the inner paths, WHEREs AND-merged. Children are already rewritten
// (the caller recurses bottom-up), so a still-nested FROM here means the child was
// NOT flattenable — merging onto it is still valid (one level fewer).
void FlattenFrom(ibQuerySelect& s)
{
	if (!s.m_from.m_subquery) return;
	if (!s.m_joins.empty() || !s.m_unions.empty()) return;   // conservative: single-source outer

	// Keep the inner select alive through the splice — `s.m_from = inner.m_from` below
	// destroys s.m_from.m_subquery, which owns it.
	const std::shared_ptr<ibQuerySelect> innerKeep = s.m_from.m_subquery;
	const ibQuerySelect& inner = *innerKeep;
	if (!InnerIsFlattenable(inner)) return;


	// Output-name -> inner column path. Empty for SELECT * (pass-through names).
	std::map<wxString, std::vector<wxString>, NoCaseLess> aliasMap;
	if (!inner.m_selectAll) {
		for (const ibQueryProjection& p : inner.m_projections) {
			const wxString name = ibQueryOutputName(p);   // the file even says so at its include
			if (!name.empty() && p.m_expr) aliasMap[name] = p.m_expr->m_path;
		}
	}

	// A dot-walk behind an output name would turn an outer aggregate / TOTALS reference
	// into a dot-walk input. Single-source dot-walk aggregates exist, but the totals /
	// having paths still validate columns differently — stay conservative and keep the
	// wrapped subquery for those shapes.
	bool outerAggregate = !s.m_groupBy.empty() || s.m_hasTotals || !s.m_totalsAggregates.empty();
	for (const ibQueryProjection& p : s.m_projections)
		if (p.m_expr && p.m_expr->m_kind == ibQueryAstExprKind::Func) outerAggregate = true;
	if (outerAggregate)
		for (const auto& kv : aliasMap)
			if (kv.second.size() > 1) return;

	// Scope check — a subquery exposes ONLY its projections. An outer reference that
	// names anything else must KEEP failing against the wrapped subquery, not silently
	// start resolving against the real table after the merge. Pre-scan; bail on a miss.
	const wxString outerAliasName = s.m_from.m_alias;
	if (!inner.m_selectAll) {
		bool outOfScope = false;
		auto checkRef = [&aliasMap, &outerAliasName, &outOfScope](ibQueryAstExpr& col) {
			const std::vector<wxString>& p = col.m_path;
			if (p.empty()) return;
			size_t head = 0;
			if (!outerAliasName.empty() && p.size() > 1 && p[0].CmpNoCase(outerAliasName) == 0)
				head = 1;
			if (aliasMap.find(p[head]) == aliasMap.end()) outOfScope = true;
		};
		for (ibQueryProjection& p : s.m_projections) WalkColumns(p.m_expr, checkRef);
		WalkColumns(s.m_where, checkRef);
		for (ibQueryAstExprPtr& g : s.m_groupBy)          WalkColumns(g, checkRef);
		WalkColumns(s.m_having, checkRef);
		for (ibQueryOrderItem& o : s.m_orderBy)           WalkColumns(o.m_expr, checkRef);
		for (ibQueryAstExprPtr& t : s.m_totalsAggregates) WalkColumns(t, checkRef);
		for (ibQueryTotalDim& d : s.m_totalsBy)
			for (ibQueryTotalField& f : d.m_fields)       WalkColumns(f.m_expr, checkRef);
		if (outOfScope) return;
	}

	// A bare-Column projection's output name must survive the substitution (SELECT pn
	// would otherwise be re-derived from the substituted path's leaf). Stamp it first.
	for (ibQueryProjection& p : s.m_projections)
		if (p.m_alias.empty())
			p.m_alias = ibQueryOutputName(p);   // its NATURAL name, written down before the path moves

	// Substitute an outer column reference: strip the subquery alias qualifier, then
	// replace an output name with its inner path (the tail of a dot-walk rides along).
	const wxString& outerAlias = outerAliasName;
	auto subst = [&aliasMap, &outerAlias](ibQueryAstExpr& col) {
		std::vector<wxString>& p = col.m_path;
		if (p.empty()) return;
		if (!outerAlias.empty() && p.size() > 1 && p[0].CmpNoCase(outerAlias) == 0)
			p.erase(p.begin());
		auto it = aliasMap.find(p[0]);
		if (it != aliasMap.end()) {
			std::vector<wxString> np = it->second;
			np.insert(np.end(), p.begin() + 1, p.end());
			p = std::move(np);
		}
	};
	for (ibQueryProjection& p : s.m_projections) WalkColumns(p.m_expr, subst);
	WalkColumns(s.m_where, subst);
	for (ibQueryAstExprPtr& g : s.m_groupBy)        WalkColumns(g, subst);
	WalkColumns(s.m_having, subst);
	for (ibQueryOrderItem& o : s.m_orderBy)         WalkColumns(o.m_expr, subst);
	for (ibQueryAstExprPtr& t : s.m_totalsAggregates) WalkColumns(t, subst);
	for (ibQueryTotalDim& d : s.m_totalsBy)
		for (ibQueryTotalField& f : d.m_fields)      WalkColumns(f.m_expr, subst);

	// The inner's ORDER BY is simply gone with the wrapper, and that is correct: a nested query has
	// no order (see InnerIsFlattenable). The order a report shows is the OUTER one — the composition's
	// own sort setting — which is the only place it is stated.

	// Outer SELECT * over an explicit inner projection = exactly the subquery's output.
	if (s.m_selectAll && !inner.m_selectAll) {
		s.m_selectAll   = false;
		s.m_projections = inner.m_projections;
	}

	// Merge the filters: outer AND inner (inner is already normalized by the child pass).
	if (s.m_where && inner.m_where) {
		auto andNode = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		andNode->m_isOr = false;
		andNode->m_lhs  = s.m_where;
		andNode->m_rhs  = inner.m_where;
		andNode->m_line = s.m_where->m_line; andNode->m_col = s.m_where->m_col;
		s.m_where = andNode;
	}
	else if (inner.m_where) {
		s.m_where = inner.m_where;
	}

	// The splice. The inner FROM alias is kept — aliasMap paths may carry it as a
	// qualifier (SELECT i.Code AS c FROM Catalog.X AS i).
	s.m_from = inner.m_from;

	// Said on this side too: a nested source that COLLAPSED never reaches the declaration road below
	// it, so without this line its absence there reads as a refusal nobody made.
	ibJournalInfo(wxT("query.rewrite"), wxT("nested FROM flattened into the outer select"));
}

#undef FlattenDecline   // the rule's own word — it ends with the rule

//////////////////////////////////////////////////////////////////////
// The pass — bottom-up over every SELECT scope
//////////////////////////////////////////////////////////////////////

// Does this condition fold rows — i.e. does an aggregate call appear anywhere inside it?
bool MentionsAggregate(const ibQueryAstExprPtr& e)
{
	if (!e)
		return false;
	if (e->m_kind == ibQueryAstExprKind::Func && ibIsAggregateKeyword(e->m_func))
		return true;
	// NOT into a nested SELECT: an aggregate in a subquery folds THAT query's rows, and moving the
	// outer condition on its account would filter the wrong thing.
	if (e->m_subquery)
		return false;
	for (const ibQueryAstExprPtr& child : { e->m_lhs, e->m_rhs, e->m_arg, e->m_low, e->m_high, e->m_else })
		if (MentionsAggregate(child)) return true;
	for (const ibQueryAstExprPtr& item : e->m_list)
		if (MentionsAggregate(item)) return true;
	for (const auto& branch : e->m_cases)
		if (MentionsAggregate(branch.first) || MentionsAggregate(branch.second)) return true;
	return false;
}

// ⭐ RULE: A CONDITION OVER A FOLDED VALUE IS A HAVING, WHEREVER IT WAS WRITTEN.
//
// `WHERE` filters ROWS and `HAVING` filters GROUPS — a real distinction, and one the author of a
// query should not have to carry. The constructor's Conditions tab offers the aggregate fields
// beside the plain ones (they are all fields of the result), so a condition over `SUM(Qty)` gets
// written where every other condition is written. Left there it reached the row filter, which has
// no aggregates to filter by, and came back as an error about a query the window itself composed.
//
// The move is per AND-TERM, because that is the granularity at which the two filters compose:
// `WHERE Warehouse = &W AND SUM(Qty) > 100` is a row filter and a group filter written together,
// and splitting it is not a change of meaning — a group filter applied after a row filter is what
// the query says either way.
//
// ⚠ AN `OR` ACROSS THE TWO MOVES WHOLE, AND IS NEVER SPLIT. `WHERE A = 1 OR SUM(x) > 5` is one
// term: splitting it into a row filter and a group filter would change which rows survive, and
// leaving it behind is not an option either — anything naming an aggregate can only be evaluated
// after the fold. So the whole term goes, and `A` must be a group key for it to resolve there,
// which is the engine's own check to make.
void MoveAggregateTermsToHaving(ibQuerySelect& s)
{
	if (!s.m_where)
		return;

	std::vector<ibQueryAstExprPtr> terms;
	ibQueryFlattenAnd(s.m_where, terms);

	std::vector<ibQueryAstExprPtr> rows, groups;
	for (const ibQueryAstExprPtr& term : terms)
		(MentionsAggregate(term) ? groups : rows).push_back(term);

	if (groups.empty())
		return;   // nothing folded — the WHERE is a row filter, as written

	s.m_where = ibQueryFoldAnd(rows);   // null when every term moved, which is correct: no row filter
	// An existing HAVING keeps its place and the moved terms join it — the author may have written
	// both, and one must not silently replace the other.
	if (s.m_having)
		groups.insert(groups.begin(), s.m_having);
	s.m_having = ibQueryFoldAnd(groups);
}

// ⭐⭐ A LINK WRITTEN AS A CONDITION IS STILL A LINK.
//
// Two tables with nothing said about how they meet are MULTIPLIED — every row of one against every
// row of the other — and the way that product is narrowed is a condition: `FROM A, B WHERE A.x =
// B.y`, which is how a join was written before anyone spelled JOIN. It is also what the constructor
// produces when the author leaves the Links tab alone and writes the relation on the Conditions tab.
//
// The door underneath takes a join key as TWO COLUMNS and a filter as COLUMN <op> VALUE, so such a
// term could not go down the WHERE road at all: the engine answered "expected a literal or a
// parameter as the comparison value" about a perfectly ordinary sentence. Here it is moved to where
// the engine can read it — the ON of the join it relates — and the query means exactly what it said.
//
// TWO GUARDS, and both are about not changing what was written:
//   * ONLY A JOIN THAT HAS NO LINK YET. One that already carries an ON is the author's, untouched.
//   * ONLY AN INNER JOIN. In an OUTER one, ON and WHERE are genuinely different: ON pre-filters the
//     null-padded side, WHERE removes the padded rows afterwards. Moving the term there would give
//     a different answer, so it stays where it was written.
//
// And it never GUESSES: only a term that names both sides by their table (`A.x = B.y`) is moved. A
// bare column says nothing about which table it stands on, and this pass does not resolve names —
// that is the lowering's job, further down, with the metadata in hand.
void LiftJoinConditions(ibQuerySelect& s)
{
	if (s.m_joins.empty() || !s.m_where)
		return;

	const auto nameOf = [](const ibQuerySource& source) -> wxString {
		if (!source.m_alias.IsEmpty())
			return source.m_alias;
		return source.m_name.empty() ? wxString() : source.m_name.back();
	};

	std::vector<wxString> names;   // 0 = the FROM, i + 1 = joins[i]
	names.push_back(nameOf(s.m_from));
	for (const ibQueryAstJoin& join : s.m_joins)
		names.push_back(nameOf(join.m_source));

	const auto sourceOf = [&names](const ibQueryAstExpr& e) -> int {
		if (e.m_kind != ibQueryAstExprKind::Column || e.m_path.size() < 2)
			return -1;
		for (size_t i = 0; i < names.size(); ++i)
			if (!names[i].IsEmpty() && names[i].IsSameAs(e.m_path[0], false))
				return static_cast<int>(i);
		return -1;
	};

	std::vector<ibQueryAstExprPtr> terms;
	ibQueryFlattenAnd(s.m_where, terms);

	std::vector<ibQueryAstExprPtr> kept;
	for (const ibQueryAstExprPtr& term : terms) {
		bool lifted = false;
		if (term && term->m_kind == ibQueryAstExprKind::Compare && term->m_lhs && term->m_rhs) {
			const int left  = sourceOf(*term->m_lhs);
			const int right = sourceOf(*term->m_rhs);
			if (left >= 0 && right >= 0 && left != right) {
				// The LATER of the two is the one whose ON can hold it: a join relates its own source
				// to something already in the tree, and the tree is built left to right.
				const size_t later = static_cast<size_t>(left > right ? left : right);
				if (later > 0 && later <= s.m_joins.size()) {
					ibQueryAstJoin& join = s.m_joins[later - 1];
					if (!join.m_on && join.m_kind == ibQueryJoinKindAst::Inner) {
						join.m_on = term;
						lifted = true;
					}
				}
			}
		}
		if (!lifted)
			kept.push_back(term);
	}
	s.m_where = ibQueryFoldAnd(kept);
}

// ⭐⭐ THE ORDER OF THE TABLES IS THE ENGINE'S BUSINESS, NOT THE AUTHOR'S.
//
// Sources are joined left to right, so a link may only relate tables already read. Add a table and
// then link it to one added AFTER it and the query is refused — correctly, but for a reason the
// author has no way to see: both are tables of the same query, and nothing on the screen says one
// of them is "later". Ordering them by hand is bookkeeping the engine can do.
//
// So a RUN of INNER joins is reordered until every link stands on tables already in scope.
//
// TWO THINGS IT WILL NOT DO, and both are about not changing what was written:
//   * AN OUTER JOIN NEVER MOVES, and it ends the run. Position IS meaning there: which side gets
//     null-padded, and what a later link sees of the padded rows, both depend on where it sits.
//     Only INNER commutes.
//   * A CYCLE IS LEFT ALONE. If no remaining join can be satisfied, the rest keep their order and
//     the consistency check says so in its own words — inventing an order for a query that has no
//     valid one would replace a clear complaint with a silent wrong answer.
//
// Names, not positions: a link mentions tables BY NAME, so what matters is which names are in scope
// when it is made. That also makes the pass safe to run twice (it is idempotent on a sound query).
void ReorderInnerJoins(ibQuerySelect& s)
{
	if (s.m_joins.size() < 2)
		return;

	const auto nameOf = [](const ibQuerySource& source) -> wxString {
		if (!source.m_alias.IsEmpty())
			return source.m_alias;
		return source.m_name.empty() ? wxString() : source.m_name.back();
	};

	// The names a link mentions — qualified paths only, for the same reason LiftJoinConditions reads
	// only those: a bare column says nothing about which table it stands on, and this pass does not
	// resolve names. Walks the whole predicate, so `a.x = b.y AND a.z = c.w` names all three.
	std::function<void(const ibQueryAstExprPtr&, std::vector<wxString>&)> mentions =
		[&mentions](const ibQueryAstExprPtr& e, std::vector<wxString>& out) {
		if (!e)
			return;
		if (e->m_kind == ibQueryAstExprKind::Column && e->m_path.size() >= 2)
			out.push_back(e->m_path.front());
		mentions(e->m_arg, out);  mentions(e->m_lhs, out);  mentions(e->m_rhs, out);
		mentions(e->m_low, out);  mentions(e->m_high, out); mentions(e->m_else, out);
		for (const ibQueryAstExprPtr& item : e->m_list) mentions(item, out);
		for (const auto& branch : e->m_cases) { mentions(branch.first, out); mentions(branch.second, out); }
		// A nested SELECT has its own scope — the names inside it say nothing about THIS query's order.
	};

	std::vector<ibQueryAstJoin> out;
	out.reserve(s.m_joins.size());

	std::vector<wxString> inScope;
	inScope.push_back(nameOf(s.m_from));

	size_t i = 0;
	while (i < s.m_joins.size()) {
		// An OUTER join is a fence: everything before it is settled, it stays where it is.
		if (s.m_joins[i].m_kind != ibQueryJoinKindAst::Inner) {
			inScope.push_back(nameOf(s.m_joins[i].m_source));
			out.push_back(s.m_joins[i]);
			++i;
			continue;
		}

		// The run of consecutive INNER joins starting here.
		size_t end = i;
		while (end < s.m_joins.size() && s.m_joins[end].m_kind == ibQueryJoinKindAst::Inner)
			++end;

		std::vector<ibQueryAstJoin> pending(s.m_joins.begin() + i, s.m_joins.begin() + end);
		while (!pending.empty()) {
			size_t pick = pending.size();   // none
			for (size_t k = 0; k < pending.size(); ++k) {
				std::vector<wxString> named;
				mentions(pending[k].m_on, named);
				const wxString own = nameOf(pending[k].m_source);
				bool satisfied = true;
				for (const wxString& n : named) {
					if (n.IsSameAs(own, false))
						continue;
					bool known = false;
					for (const wxString& have : inScope)
						if (have.IsSameAs(n, false)) { known = true; break; }
					if (!known) { satisfied = false; break; }
				}
				if (satisfied) { pick = k; break; }
			}
			if (pick == pending.size()) {
				// A cycle, or a link naming something outside this query. Leave the remainder as
				// written — the complaint belongs to the check, with the author's own order in it.
				for (const ibQueryAstJoin& rest : pending) {
					inScope.push_back(nameOf(rest.m_source));
					out.push_back(rest);
				}
				break;
			}
			inScope.push_back(nameOf(pending[pick].m_source));
			out.push_back(pending[pick]);
			pending.erase(pending.begin() + pick);
		}
		i = end;
	}

	s.m_joins.swap(out);
}

void RewriteSelectInPlace(ibQuerySelect& s)
{
	// Children first: a flattenable child collapses before the parent looks at it.
	if (s.m_from.m_subquery) RewriteSelectInPlace(*s.m_from.m_subquery);
	for (ibQueryAstJoin& j : s.m_joins)
		if (j.m_source.m_subquery) RewriteSelectInPlace(*j.m_source.m_subquery);
	for (const std::shared_ptr<ibQuerySelect>& u : s.m_unions)
		RewriteSelectInPlace(*u);

	if (s.m_where) {
		s.m_where = NormalizeNeg(s.m_where);
		WalkInSubqueries(s.m_where, RewriteSelectInPlace);   // IN (SELECT …) — own scope
		MoveAggregateTermsToHaving(s);
		// AFTER the aggregate move (a fold belongs in HAVING and is nobody's join key) and BEFORE
		// FlattenFrom, which may bring a subquery's own joins up into this list.
		LiftJoinConditions(s);
	}

	// AFTER the lift, because a term moved into an empty ON changes which tables that link names —
	// and therefore where it may stand.
	ReorderInnerJoins(s);

	FlattenFrom(s);
}

} // namespace

//////////////////////////////////////////////////////////////////////
// ibQueryRewrite
//////////////////////////////////////////////////////////////////////

ibQuerySelectPtr ibQueryRewrite::Rewrite(const ibQuerySelect& ast)
{
	ibQuerySelectPtr clone = CloneSelect(ast);
	RewriteSelectInPlace(*clone);
	return clone;
}

// The clone WITHOUT the rules — see the header. Same walker, so a copy can never drift from what
// the pass considers a complete tree.
ibQuerySelectPtr ibQueryRewrite::Clone(const ibQuerySelect& ast)
{
	return CloneSelect(ast);
}

ibQuerySelectPtr ibQueryRewrite::ReorderJoins(const ibQuerySelect& ast)
{
	ibQuerySelectPtr clone = CloneSelect(ast);
	ReorderInnerJoins(*clone);
	return clone;
}

bool ibQueryIsTrueLiteral(const ibQueryAstExprPtr& expr)
{
	return expr && expr->m_kind == ibQueryAstExprKind::Literal && expr->m_literal.GetBoolean();
}

void ibQueryFlattenAnd(const ibQueryAstExprPtr& expr, std::vector<ibQueryAstExprPtr>& out)
{
	if (!expr)
		return;
	// Anything that is not an AND is ONE condition — including an OR, which is a condition and not
	// a list of them. That is why an OR row shows in the constructor as a single (arbitrary) line.
	if (expr->m_kind == ibQueryAstExprKind::Logical && !expr->m_isOr) {
		ibQueryFlattenAnd(expr->m_lhs, out);
		ibQueryFlattenAnd(expr->m_rhs, out);
		return;
	}
	out.push_back(expr);
}

void ibQueryMoveAggregateConditionsToHaving(ibQuerySelect& select)
{
	MoveAggregateTermsToHaving(select);
}

ibQueryAstExprPtr ibQueryFoldAnd(const std::vector<ibQueryAstExprPtr>& rows)
{
	ibQueryAstExprPtr folded;
	for (const ibQueryAstExprPtr& row : rows) {
		if (!row) continue;
		if (!folded) { folded = row; continue; }
		auto node = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		node->m_isOr = false;
		node->m_lhs = folded;
		node->m_rhs = row;
		folded = node;
	}
	return folded;
}

// ===========================================================================
//  NAMING — what the host calls when it ADDS, so CheckNames never has to refuse
// ===========================================================================

namespace {

// (The name a source answers to is ibQuerySourceName — queryRender.h, one answer for the whole
// product. It used to be written out here as well, and in six other places.)

// Does this segment name a SOURCE of the select, rather than open a walk?
bool NamesASource(const ibQuerySelect& select, const wxString& segment)
{
	if (ibQuerySourceName(select.m_from).IsSameAs(segment, false))
		return true;
	for (const ibQueryAstJoin& join : select.m_joins)
		if (ibQuerySourceName(join.m_source).IsSameAs(segment, false))
			return true;
	return false;
}

} // namespace

wxString ibQueryProposedName(const ibQuerySelect& select, const ibQueryProjection& projection)
{
	if (!projection.m_alias.IsEmpty())
		return projection.m_alias;
	if (!projection.m_expr || projection.m_expr->m_kind != ibQueryAstExprKind::Column)
		return wxEmptyString;

	const std::vector<wxString>& path = projection.m_expr->m_path;
	if (path.empty())
		return wxEmptyString;

	// The qualifier goes; the walk stays. `Catalog1.Reference.PredefinedName` -> `ReferencePredefinedName`.
	const size_t first = (path.size() > 1 && NamesASource(select, path[0])) ? 1 : 0;

	// A legal identifier by construction: every segment already is one.
	wxString name;
	for (size_t i = first; i < path.size(); ++i)
		name += path[i];
	return name;
}

void ibQueryEnsureUniqueName(ibQuerySelect& select, ibQueryProjection& projection)
{
	auto taken = [&select, &projection](const wxString& name) {
		for (const ibQueryProjection& other : select.m_projections)
			if (&other != &projection && ibQueryProposedName(select, other).IsSameAs(name, false))
				return true;
		return false;
	};

	const wxString wanted = ibQueryProposedName(select, projection);
	if (wanted.IsEmpty()) {
		// The default NAME, not a default suffix: `Field` numbered from 1. The counter runs over
		// what is already there, exactly as the duplicate numbering below does, so the two cannot
		// hand out the same name.
		for (unsigned int n = 1; ; ++n) {
			const wxString candidate = wxString::Format(_("Field%u"), n);
			if (!taken(candidate)) {
				projection.m_alias = candidate;
				return;
			}
		}
	}

	// ⚠ A NAME THAT IS NOT THE NATURAL ONE HAS TO BE WRITTEN DOWN.
	//
	// The natural name of a projection is the LEAF of its path (ibQueryOutputName) — that is what
	// the language reads it back by when no alias is given. For a WALK the leaf is the wrong word:
	// `Reference.PredefinedName` and `PredefinedName` are two different columns whose leaf is the
	// same, and leaving both unaliased makes the engine refuse the query for duplicate output names.
	//
	// So the walk's name is stored AS AN ALIAS even when nothing collides yet. Computing a name and
	// then not writing it was the whole defect: the query still said `Catalog1.Reference.PredefinedName`
	// with no alias, so everything downstream — the field map, the check, the result — went on calling
	// it `PredefinedName`.
	if (projection.m_alias.IsEmpty() && !ibQueryOutputName(projection).IsSameAs(wanted, false))
		projection.m_alias = wanted;

	if (!taken(wanted))
		return;

	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = wanted + wxString::Format(wxT("%u"), n);
		if (!taken(candidate)) {
			projection.m_alias = candidate;
			return;
		}
	}
}

wxString ibQueryUniqueSourceAlias(const ibQuerySelect& select, const wxString& wanted,
                                  const ibQuerySource* except)
{
	if (wanted.IsEmpty())
		return wanted;

	std::vector<const ibQuerySource*> sources;
	sources.push_back(&select.m_from);
	for (const ibQueryAstJoin& join : select.m_joins)
		sources.push_back(&join.m_source);

	// THE NAME A SOURCE ANSWERS TO — its alias when it has one, else the last segment of its path.
	auto taken = [&sources, except](const wxString& name) {
		for (const ibQuerySource* source : sources) {
			if (source == nullptr || source == except)
				continue;
			if (ibQuerySourceName(*source).IsSameAs(name, false))
				return true;
		}
		return false;
	};

	if (!taken(wanted))
		return wanted;
	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = wanted + wxString::Format(wxT("%u"), n);
		if (!taken(candidate))
			return candidate;
	}
}
