////////////////////////////////////////////////////////////////////////////
//	L4 — optimizer rewrite pass: negation normalization + FROM-subquery
//	flattening (queryRewrite.h). Pure AST -> AST; runs on a deep clone.
////////////////////////////////////////////////////////////////////////////

#include "queryRewrite.h"

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
	for (ibQueryOrderItem& o : c->m_orderBy)
		o.m_expr = CloneExpr(o.m_expr);
	for (ibQueryAstExprPtr& t : c->m_totalsAggregates)
		t = CloneExpr(t);
	for (ibQueryTotalDim& d : c->m_totalsBy)
		d.m_expr = CloneExpr(d.m_expr);
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
bool InnerIsFlattenable(const ibQuerySelect& inner)
{
	if (!inner.m_joins.empty() || !inner.m_unions.empty() || inner.m_hasTotals) return false;
	if (inner.m_distinct || !inner.m_groupBy.empty() || inner.m_having)         return false;
	if (!inner.m_orderBy.empty())                                               return false;
	if (inner.m_selectAll) return true;
	for (const ibQueryProjection& p : inner.m_projections) {
		if (p.m_star) return false;
		if (!p.m_expr || p.m_expr->m_kind != ibQueryAstExprKind::Column) return false;
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
			const wxString name = !p.m_alias.empty()
				? p.m_alias
				: (p.m_expr->m_path.empty() ? wxString() : p.m_expr->m_path.back());
			if (!name.empty()) aliasMap[name] = p.m_expr->m_path;
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
		for (ibQueryTotalDim& d : s.m_totalsBy)           WalkColumns(d.m_expr, checkRef);
		if (outOfScope) return;
	}

	// A bare-Column projection's output name must survive the substitution (SELECT pn
	// would otherwise be re-derived from the substituted path's leaf). Stamp it first.
	for (ibQueryProjection& p : s.m_projections)
		if (p.m_alias.empty() && p.m_expr && p.m_expr->m_kind == ibQueryAstExprKind::Column
			&& !p.m_expr->m_path.empty())
			p.m_alias = p.m_expr->m_path.back();

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
	for (ibQueryTotalDim& d : s.m_totalsBy)         WalkColumns(d.m_expr, subst);

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
}

//////////////////////////////////////////////////////////////////////
// The pass — bottom-up over every SELECT scope
//////////////////////////////////////////////////////////////////////

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
	}

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
