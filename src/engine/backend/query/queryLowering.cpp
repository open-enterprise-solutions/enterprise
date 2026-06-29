////////////////////////////////////////////////////////////////////////////
//	L4-1 — lowering: AST + parameters -> ibDataQueryBuilder, executed (queryLowering.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLowering.h"

#include "queryRewrite.h"                 // ibQueryRewrite — optimizer pass (AST -> AST)
#include "queryable.h"                    // ibBackendQueryColumn / ibQueryFilterOp
#include "queryableFactory.h"             // ibQueryableFactory — source-namespace resolution
#include "backend/appData.h"              // ibApplicationData::GetQueryableFactory
#include "backend/tableInfo.h"            // ibComparisonType
#include "backend/backend_exception.h"    // ibBackendCoreException

namespace {

// The output-column descriptor the runtime reads back — used unqualified throughout this namespace.
using OutputColumn = ibQueryLowering::OutputColumn;

// All lowering errors carry the AST source span. Always throws (callers that return
// a value follow with an unreachable dummy return — the codebase's Error();return idiom).
void Fail(unsigned int line, unsigned int col, const wxString& msg)
{
	ibBackendCoreException::Error(_("Query: %s (line %u, position %u)"), msg, line, col);
}

ibValue EvalValue(const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// Resolve the FROM / JOIN source namespace.name to a queryable through the source
// factory appData owns (built-in metaobject families + plugin / external sources).
const ibBackendQueryable* ResolveSource(const ibQuerySource& src, const std::map<wxString, ibValue>& params)
{
	if (src.m_name.size() < 2)
		Fail(0, 0, _("a source must be <Kind>.<Name>"));

	// ns = the first segment (the metaclass kind — English canonical, as the descriptors
	// register); the rest join into the object name, so a virtual table reads as
	// <Kind>.<Object>.<Table> -> name="Object.Table" (the register's balance / turnover / slice
	// descriptor registers under that composite name).
	const wxString& ns = src.m_name[0];
	wxString name = src.m_name[1];
	for (size_t i = 2; i < src.m_name.size(); ++i)
		name += wxT(".") + src.m_name[i];

	ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory();
	if (factory == nullptr) {
		Fail(0, 0, _("the query engine is not available (no application data)"));
		return nullptr;
	}
	if (!factory->HasNamespace(ns)) {
		Fail(0, 0, wxString::Format(_("unknown metaobject kind '%s'"), ns));
		return nullptr;
	}

	// Source-call args (Balance(&Period, …)) -> the descriptor's CreateQueryable. The companion copies
	// them by value (e.g. ibBalanceQueryable stores m_period), so these locals may die after Resolve.
	std::vector<ibValue>  argVals;
	for (const ibQueryAstExprPtr& a : src.m_args)
		argVals.push_back(EvalValue(*a, params));
	std::vector<ibValue*> argPtrs;
	for (ibValue& v : argVals)
		argPtrs.push_back(&v);

	const ibBackendQueryable* q = factory->Resolve(ns, name,
		argPtrs.empty() ? nullptr : argPtrs.data(), static_cast<long>(argPtrs.size()));
	if (q == nullptr) {
		Fail(0, 0, wxString::Format(_("metaobject '%s.%s' not found or cannot be queried"), ns, name));
		return nullptr;
	}
	return q;
}

// One resolved source in a query: its alias + queryable. A single-source query is a list of one
// (alias may be empty); a JOIN adds one binding per joined source. Column resolution picks the
// source by alias prefix (`b.Field`), else searches all sources (the primary, sources[0], first).
struct ibSourceBinding
{
	wxString                    m_alias;
	const ibBackendQueryable*   m_q = nullptr;
};

// Find the source an alias names, or null if the first path segment is not a known alias.
const ibBackendQueryable* SourceForAlias(const std::vector<ibSourceBinding>& sources, const wxString& alias)
{
	if (!alias.empty())
		for (const ibSourceBinding& s : sources)
			if (!s.m_alias.empty() && s.m_alias.CmpNoCase(alias) == 0) return s.m_q;
	return nullptr;
}

// A SINGLE column (for WHERE / ORDER / aggregate arg / GROUP BY): `alias.col` -> the aliased
// source's column; bare `col` -> the first source that owns it (primary first). Dot-walk paths
// are rejected here (only the projection resolves them, via ResolvePath).
const ibBackendQueryColumn* ResolveColumnSingle(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;

	if (path.size() == 2) {
		const ibBackendQueryable* q = SourceForAlias(sources, path[0]);
		if (q != nullptr) {
			const ibBackendQueryColumn* col = q->ResolveColumnByName(path[1]);
			if (col == nullptr)
				Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[1]));
			return col;
		}
		// path[0] is not an alias -> a dot-walk (Producer.Name), not allowed in this clause yet.
	}
	else if (path.size() == 1) {
		for (const ibSourceBinding& s : sources) {
			const ibBackendQueryColumn* col = s.m_q->ResolveColumnByName(path[0]);
			if (col != nullptr) return col;
		}
		Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[0]));
		return nullptr;
	}

	Fail(e.m_line, e.m_col, _("dot-walk columns are not supported in this clause yet"));
	return nullptr;
}

// A reference dot-walk path (Producer.Name | b.Producer.Name) -> the chain of columns SelectPath
// wants. An alias prefix selects the starting source; otherwise the walk starts on the source that
// owns the first segment (primary first).
std::vector<const ibBackendQueryColumn*> ResolvePath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;
	size_t i = 0;
	const ibBackendQueryable* cur = sources.empty() ? nullptr : sources[0].m_q;

	if (path.size() >= 2) {
		const ibBackendQueryable* aliased = SourceForAlias(sources, path[0]);
		if (aliased != nullptr) { cur = aliased; i = 1; }
	}
	if (i == 0)   // unqualified — start on the source that owns the first segment
		for (const ibSourceBinding& s : sources)
			if (s.m_q->ResolveColumnByName(path[0]) != nullptr) { cur = s.m_q; break; }

	std::vector<const ibBackendQueryColumn*> cols;
	for (size_t k = i; k < path.size(); ++k) {
		const ibBackendQueryColumn* col = cur->ResolveColumnByName(path[k]);
		if (col == nullptr) {
			Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[k]));
			return {};
		}
		cols.push_back(col);
		if (k + 1 < path.size()) {
			const ibBackendQueryable* next = cur->ResolveReferenceTarget(col);
			if (next == nullptr) {
				// A COMPOSITE (multi-type) reference at ANY segment: resolve the REPRESENTATIVE chain through
				// the FIRST target (attribute names are the same across types); the provider (BuildPageIR)
				// re-resolves + BRANCHES per type — one JOIN sub-tree per target, COALESCE the leaf. A
				// composite mid-segment forks the path; the provider's recursive walk handles the tree.
				const std::vector<const ibBackendQueryable*> targets = cur->ResolveReferenceTargets(col);
				if (targets.empty()) {
					Fail(e.m_line, e.m_col,
						wxString::Format(_("'%s' is not a single-target reference (cannot walk)"), path[k]));
					return {};
				}
				next = targets.front();
			}
			cur = next;
		}
	}
	return cols;
}

// The queryable that owns the FIRST segment of a path — the dot-walk root. Mirrors ResolvePath's
// start: a qualified `alias.col…` starts on the aliased source, an unqualified one on the source
// that owns the first segment. (Used to expand a dot-walk over a multi-source builder.)
const ibBackendQueryable* RootForPath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;
	if (path.size() >= 2) {
		const ibBackendQueryable* aliased = SourceForAlias(sources, path[0]);
		if (aliased != nullptr) return aliased;
	}
	for (const ibSourceBinding& s : sources)
		if (s.m_q->ResolveColumnByName(path[0]) != nullptr) return s.m_q;
	return sources.empty() ? nullptr : sources[0].m_q;
}

// Expand a reference dot-walk path (size > 1) over a MULTI-SOURCE builder. `ibRefJoinChain` builds
// SQL joins for the single-source door; the RAM stitch has none, so here each NON-leaf segment's
// reference target becomes an explicit LEFT-join leaf, keyed EXACTLY on (segment ref column, target
// self-reference = GetPrimaryKeyColumns().front()) — no `ReferenceColumnTo` ambiguity when a leaf has
// two references to the same target. Joins are deduped by path prefix (`joined`: prefix-key -> target),
// so `c.Owner.Region` and `c.Owner.Code` share ONE Owner join. Returns the leaf column (pathCols.back(),
// owned by the final target) to group / project by — a plain qualified column in the composed snapshot.
const ibBackendQueryColumn* ExpandDotWalkJoins(
	ibDataQueryBuilder& b, const ibBackendQueryable* rootQ,
	const std::vector<const ibBackendQueryColumn*>& pathCols,
	std::map<wxString, const ibBackendQueryable*>& joined, int& aliasSeq, const ibQueryAstExpr& e)
{
	const ibBackendQueryable* curQ = rootQ;
	wxString prefixKey;
	for (size_t i = 0; i + 1 < pathCols.size(); ++i) {
		const ibBackendQueryColumn* refCol = pathCols[i];
		const ibBackendQueryable* tgtQ = (curQ != nullptr) ? curQ->ResolveReferenceTarget(refCol) : nullptr;
		const std::vector<const ibBackendQueryColumn*> tgtKeys = (tgtQ != nullptr) ? tgtQ->GetPrimaryKeyColumns()
		                                                                           : std::vector<const ibBackendQueryColumn*>{};
		if (tgtQ == nullptr || tgtKeys.empty())
			Fail(e.m_line, e.m_col,
				_("a dot-walk segment over a JOIN/UNION must be a single-target catalog/document reference"));
		prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
		if (joined.find(prefixKey) == joined.end()) {
			const wxString alias = wxString::Format(wxT("_dw%d"), aliasSeq++);
			b.Join(tgtQ, refCol, tgtKeys.front(), ibQueryJoinKind::Left, alias);   // explicit keys — no ambiguity
			joined[prefixKey] = tgtQ;
		}
		curQ = tgtQ;
	}
	return pathCols.back();   // owned by the final target — a plain qualified column in the snapshot
}

// Map the L4 AST join kind to the L3 join kind.
ibQueryJoinKind MapJoinKind(ibQueryJoinKindAst k)
{
	switch (k) {
		case ibQueryJoinKindAst::Left:  return ibQueryJoinKind::Left;
		case ibQueryJoinKindAst::Right: return ibQueryJoinKind::Right;
		case ibQueryJoinKindAst::Full:  return ibQueryJoinKind::Full;
		default:                        return ibQueryJoinKind::Inner;
	}
}

// Declare a NAMED ref-join: `JOIN root.refA[.refB…] AS alias` auto-joins the reference chain off `root` and
// binds the FINAL target to `alias` in `sources`, so a later `alias.field` resolves as a clean qualified
// column. Every segment is a single-target reference; intermediate targets get synthetic aliases, the last
// gets the user's alias. Reuses ExpandDotWalkJoins' key derivation (segment ref col, target self-reference).
void ExpandRefJoinAlias(ibDataQueryBuilder& b, std::vector<ibSourceBinding>& sources,
	const ibBackendQueryable* rootQ, const std::vector<wxString>& segs,
	const wxString& finalAlias, ibQueryJoinKind kind, int& aliasSeq)
{
	const ibBackendQueryable* curQ = rootQ;
	for (size_t i = 0; i < segs.size(); ++i) {
		const ibBackendQueryColumn* refCol = (curQ != nullptr) ? curQ->ResolveColumnByName(segs[i]) : nullptr;
		const ibBackendQueryable*   tgtQ   = (curQ != nullptr && refCol != nullptr) ? curQ->ResolveReferenceTarget(refCol) : nullptr;
		const std::vector<const ibBackendQueryColumn*> tgtKeys = (tgtQ != nullptr) ? tgtQ->GetPrimaryKeyColumns()
		                                                                           : std::vector<const ibBackendQueryColumn*>{};
		if (refCol == nullptr || tgtQ == nullptr || tgtKeys.empty())
			Fail(0, 0, wxString::Format(_("'%s' is not a single-target reference for a named ref-join (AS)"), segs[i]));
		const bool last = (i + 1 == segs.size());
		const wxString alias = last ? finalAlias : wxString::Format(wxT("_rj%d"), aliasSeq++);
		b.Join(tgtQ, refCol, tgtKeys.front(), last ? kind : ibQueryJoinKind::Left, alias);
		sources.push_back({ alias, tgtQ });
		curQ = tgtQ;
	}
}

// Map the L4 AST comparison op to the L3 join op (the join node carries the L3 enum so L3 stays L4-agnostic).
ibJoinCompareOp MapJoinOp(ibQueryCompareOp op)
{
	switch (op) {
		case ibQueryCompareOp::Ne: return ibJoinCompareOp::Ne;
		case ibQueryCompareOp::Lt: return ibJoinCompareOp::Lt;
		case ibQueryCompareOp::Le: return ibJoinCompareOp::Le;
		case ibQueryCompareOp::Gt: return ibJoinCompareOp::Gt;
		case ibQueryCompareOp::Ge: return ibJoinCompareOp::Ge;
		default:                   return ibJoinCompareOp::Eq;
	}
}

const ibValue* FindParam(const std::map<wxString, ibValue>& params, const wxString& name)
{
	for (const auto& kv : params)
		if (kv.first.CmpNoCase(name) == 0) return &kv.second;
	return nullptr;
}

ibValue EvalValue(const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	if (e.m_kind == ibQueryAstExprKind::Literal) return e.m_literal;
	if (e.m_kind == ibQueryAstExprKind::Param) {
		const ibValue* v = FindParam(params, e.m_paramName);
		if (v == nullptr) {
			Fail(e.m_line, e.m_col, wxString::Format(_("parameter '&%s' is not set"), e.m_paramName));
			return ibValue();
		}
		return *v;
	}
	Fail(e.m_line, e.m_col, _("expected a literal or a parameter as the comparison value"));
	return ibValue();
}

ibAggregateFn AggFn(ibQueryKeyword kw)
{
	switch (kw) {
	case ibQueryKeyword::Sum:   return ibAggregateFn::Sum;
	case ibQueryKeyword::Count: return ibAggregateFn::Count;
	case ibQueryKeyword::Min:   return ibAggregateFn::Min;
	case ibQueryKeyword::Max:   return ibAggregateFn::Max;
	case ibQueryKeyword::Avg:   return ibAggregateFn::Avg;
	default:                    return ibAggregateFn::Count;
	}
}

// Forward decls used by IN (subquery): build a sub-SELECT into a queryable (owned by `owner`).
using ibSubqueryOwner = std::vector<std::unique_ptr<ibSubqueryQueryable>>;
const ibBackendQueryable* WrapSelectAsQueryable(const ibQuerySelect& sel,
                                                const std::map<wxString, ibValue>& params, ibSubqueryOwner& owner);

// --- WHERE-leaf condition builders. `path` (size > 1) = a reference dot-walk; the provider joins it
// and qualifies the LEAF (== path.back()) by the join alias. A plain column passes path = {col}. ------
ibQueryCondition CondEq(const std::vector<const ibBackendQueryColumn*>& path, const ibValue& v, bool notEqual = false)
{
	ibQueryCondition c;
	c.m_col        = path.back();
	c.m_value      = v;
	c.m_comparison = notEqual ? ibComparisonType::ibComparisonType_NotEqual
	                          : ibComparisonType::ibComparisonType_Equal;   // m_explicitOp stays false
	if (path.size() > 1) c.m_path = path;
	return c;
}
ibQueryCondition CondOp(const std::vector<const ibBackendQueryColumn*>& path, ibQueryFilterOp op, const ibValue& v)
{
	ibQueryCondition c;
	c.m_col = path.back(); c.m_value = v; c.m_explicitOp = true; c.m_op = op;
	if (path.size() > 1) c.m_path = path;
	return c;
}

std::vector<const ibBackendQueryColumn*> ResolveWhereTarget(const std::vector<ibSourceBinding>& sources,
                                                            const ibQueryAstExpr& e, bool allowDotWalk);   // defined below

ibQueryColumnExprPtr BuildColumnExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// Is this AST expression a COMPUTED WHERE / aggregate-input lhs (arithmetic or CASE)?
bool IsComputedExprAst(const ibQueryAstExpr& e)
{
	return e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case;
}

// Gate for a computed (arithmetic / CASE) condition lhs or aggregate input: the provider lowers
// it server-side on a single DB source only — the RAM stitch and computed sources do not
// evaluate condition expressions, and a silent drop would widen the filter (wrong rows).
void GateComputedExpr(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	if (sources.size() > 1)
		Fail(e.m_line, e.m_col, _("an arithmetic / CASE expression here is not yet supported over a JOIN"));
	if (!sources.empty() && sources[0].m_q != nullptr && sources[0].m_q->IsComputedInRam())
		Fail(e.m_line, e.m_col, _("an arithmetic / CASE expression here is not yet supported over a computed source (subquery / virtual table)"));
}

// Build the full boolean WHERE as an L3 predicate TREE (ibQueryPredicate). The door lowers it to
// the L2 IR (OR/NOT/IS NULL all expressible there). IN expands to Or(Eq …), BETWEEN to And(>=, <=),
// NOT IN / NOT BETWEEN / NOT LIKE wrap the positive form in Not — so the tree needs no dedicated node.
// Compare / LIKE / BETWEEN leaves carry a reference dot-walk PATH (the leaf condition's m_path) when
// allowDotWalk; the provider joins it (single-source non-aggregate read). IN / IS NULL stay plain-column
// (no path leaf on those nodes yet). Used for single-source queries + co-located JOIN booleans.
ibQueryPredicatePtr BuildWherePredicate(const std::vector<ibSourceBinding>& sources,
                                        const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params,
                                        bool allowDotWalk)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical:
		return ibQueryPredicate::Compose(
			e.m_isOr ? ibQueryPredicateKind::Or : ibQueryPredicateKind::And,
			BuildWherePredicate(sources, *e.m_lhs, params, allowDotWalk),
			BuildWherePredicate(sources, *e.m_rhs, params, allowDotWalk));

	case ibQueryAstExprKind::Not:
		return ibQueryPredicate::Not(BuildWherePredicate(sources, *e.m_lhs, params, allowDotWalk));

	case ibQueryAstExprKind::Compare: {
		// COMPUTED lhs — `Qty * Price > value`, a CASE: the leaf carries the lowered expression
		// (m_expr); the provider compares BuildColumnExpr(lhs) to the value. Gated single-source DB.
		if (IsComputedExprAst(*e.m_lhs)) {
			GateComputedExpr(sources, *e.m_lhs);
			ibQueryCondition c;
			c.m_value = EvalValue(*e.m_rhs, params);
			c.m_expr  = BuildColumnExprFromAst(sources, *e.m_lhs, params);
			switch (e.m_cmp) {
			case ibQueryCompareOp::Eq:                                                            break;
			case ibQueryCompareOp::Ne: c.m_comparison = ibComparisonType::ibComparisonType_NotEqual; break;
			case ibQueryCompareOp::Lt: c.m_explicitOp = true; c.m_op = ibQueryFilterOp::Less;         break;
			case ibQueryCompareOp::Le: c.m_explicitOp = true; c.m_op = ibQueryFilterOp::LessEqual;    break;
			case ibQueryCompareOp::Gt: c.m_explicitOp = true; c.m_op = ibQueryFilterOp::Greater;      break;
			case ibQueryCompareOp::Ge: c.m_explicitOp = true; c.m_op = ibQueryFilterOp::GreaterEqual; break;
			}
			return ibQueryPredicate::Leaf(c);
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		switch (e.m_cmp) {
		case ibQueryCompareOp::Eq: return ibQueryPredicate::Leaf(CondEq(cols, val));
		case ibQueryCompareOp::Ne: return ibQueryPredicate::Leaf(CondEq(cols, val, /*notEqual*/true));
		case ibQueryCompareOp::Lt: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Less,         val));
		case ibQueryCompareOp::Le: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::LessEqual,    val));
		case ibQueryCompareOp::Gt: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Greater,      val));
		case ibQueryCompareOp::Ge: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::GreaterEqual, val));
		}
		return nullptr;
	}

	case ibQueryAstExprKind::Like: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		ibQueryPredicatePtr like = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Like, EvalValue(*e.m_rhs, params)));
		return e.m_negated ? ibQueryPredicate::Not(like) : like;
	}

	case ibQueryAstExprKind::Between: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		ibQueryPredicatePtr lo = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::GreaterEqual, EvalValue(*e.m_low,  params)));
		ibQueryPredicatePtr hi = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::LessEqual,    EvalValue(*e.m_high, params)));
		ibQueryPredicatePtr between = ibQueryPredicate::Compose(ibQueryPredicateKind::And, lo, hi);
		return e.m_negated ? ibQueryPredicate::Not(between) : between;
	}

	case ibQueryAstExprKind::In: {
		// col IN (a, b, …)  ->  Or(col=a, col=b, …); NOT IN -> Not of that. Empty list = a vacuous
		// FALSE (Or of nothing); the door tree treats a null child as no-constraint, so guard it.
		// The leaf may be a reference dot-walk (every Eq shares the path -> one join, prefix deduped).
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);

		// Collect the IN values: either the literal list, or — for IN (subquery) — the (uncorrelated)
		// inner SELECT's single output column, materialised eagerly into a value list.
		std::vector<ibValue> values;
		if (e.m_subquery) {
			ibSubqueryOwner localOwner;   // the inner queryable lives only for this materialisation
			const ibBackendQueryable* subq = WrapSelectAsQueryable(*e.m_subquery, params, localOwner);
			const std::vector<const ibBackendQueryColumn*> outCols = subq->GetColumns();
			if (outCols.size() != 1 || outCols.front() == nullptr)
				Fail(e.m_line, e.m_col, _("IN (subquery) must SELECT exactly one column"));
			ibDataQueryBuilder sq;
			sq.From(subq);
			sq.Select(outCols.front(), wxT("v"));
			ibDataQueryResult r = sq.Execute(ibReadPageRequest{});
			while (r.Next())
				values.push_back(r.GetColumn(wxT("v")));
		}
		else {
			for (const ibQueryAstExprPtr& item : e.m_list)
				values.push_back(EvalValue(*item, params));
		}

		ibQueryPredicatePtr acc;
		for (const ibValue& v : values) {
			ibQueryPredicatePtr eq = ibQueryPredicate::Leaf(CondEq(cols, v));
			acc = acc ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, acc, eq) : eq;
		}
		if (!acc) {
			// Empty IN ( ) — matches NOTHING. Encode as a contradiction (col IS NULL AND col IS NOT NULL);
			// NOT IN of an empty set then matches everything (the outer Not below).
			acc = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
			                                ibQueryPredicate::Null(cols.back(), /*negated*/false, cols),
			                                ibQueryPredicate::Null(cols.back(), /*negated*/true,  cols));
		}
		return e.m_negated ? ibQueryPredicate::Not(acc) : acc;
	}

	case ibQueryAstExprKind::IsNull: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		return ibQueryPredicate::Null(cols.back(), e.m_negated, cols);   // m_negated = IS NOT NULL; path = dot-walk
	}

	case ibQueryAstExprKind::Column: {
		// A BARE column / dot-walk used as a predicate is a TRUTHY test on a Boolean column:
		// `WHERE Field.Seller`  ==  `WHERE Field.Seller = TRUE`. (`= TRUE` lowers as a plain Compare above.)
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, e, allowDotWalk);
		return ibQueryPredicate::Leaf(CondEq(cols, ibValue(true)));
	}

	default:
		Fail(e.m_line, e.m_col, _("unsupported WHERE expression"));
		return nullptr;
	}
}

// Lower a WHERE for a MULTI-source (JOIN) query to the door's FLAT verb conditions. The provider's
// co-located join path partitions m_conditions per leaf (by OwnsColumn) and AND-folds them — it does
// NOT yet lower the boolean predicate tree across joined leaves. So a JOIN WHERE is restricted to an
// Resolve a WHERE / ORDER target to a column PATH: size 1 = a plain column, >1 = a reference dot-walk
// (Producer.Region). Dot-walk is only realizable in a single-source, non-aggregate read (BuildPageIR
// builds the join + qualifies the leaf); reject it elsewhere rather than let the aggregate / stitch
// paths silently drop the filter. allowDotWalk = (single source AND not aggregate).
std::vector<const ibBackendQueryColumn*> ResolveWhereTarget(const std::vector<ibSourceBinding>& sources,
                                                            const ibQueryAstExpr& e, bool allowDotWalk)
{
	if (e.m_kind != ibQueryAstExprKind::Column || e.m_path.empty())
		Fail(e.m_line, e.m_col, _("expected a column (or a reference dot-walk path) here"));
	std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
	if (cols.empty())
		Fail(e.m_line, e.m_col, _("could not resolve the column"));
	if (cols.size() > 1 && !allowDotWalk)
		Fail(e.m_line, e.m_col, _("a reference dot-walk here needs a single, non-aggregate source"));
	return cols;
}

// Flat AND-tree WHERE -> the door's verb conditions. Plain columns AND reference dot-walks (the leaf
// of a path, joined by the provider). Used for a flat single-source WHERE (dot-walk allowed) and a
// flat JOIN WHERE (dot-walk rejected — the composer has no per-leaf dot-walk join yet). OR / NOT / IN
// / IS NULL never reach here (IsFlatAndWhere routes them to the predicate tree).
void LowerFlatWhere(ibDataQueryBuilder& b, const std::vector<ibSourceBinding>& sources,
                    const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params, bool allowDotWalk)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical:
		if (e.m_isOr)
			Fail(e.m_line, e.m_col, _("OR in this WHERE is not lowered to flat conditions"));
		LowerFlatWhere(b, sources, *e.m_lhs, params, allowDotWalk);
		LowerFlatWhere(b, sources, *e.m_rhs, params, allowDotWalk);
		return;

	case ibQueryAstExprKind::Compare: {
		// COMPUTED lhs — route to the door's expression verbs (single DB source; gated).
		if (IsComputedExprAst(*e.m_lhs)) {
			GateComputedExpr(sources, *e.m_lhs);
			const ibQueryColumnExprPtr lhs = BuildColumnExprFromAst(sources, *e.m_lhs, params);
			const ibValue val = EvalValue(*e.m_rhs, params);
			switch (e.m_cmp) {
			case ibQueryCompareOp::Eq: b.WhereExpr(lhs, ibComparisonType::ibComparisonType_Equal,    val); break;
			case ibQueryCompareOp::Ne: b.WhereExpr(lhs, ibComparisonType::ibComparisonType_NotEqual, val); break;
			case ibQueryCompareOp::Lt: b.WhereExprCompare(lhs, ibQueryFilterOp::Less,         val); break;
			case ibQueryCompareOp::Le: b.WhereExprCompare(lhs, ibQueryFilterOp::LessEqual,    val); break;
			case ibQueryCompareOp::Gt: b.WhereExprCompare(lhs, ibQueryFilterOp::Greater,      val); break;
			case ibQueryCompareOp::Ge: b.WhereExprCompare(lhs, ibQueryFilterOp::GreaterEqual, val); break;
			}
			return;
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		const bool walk = cols.size() > 1;
		switch (e.m_cmp) {
		case ibQueryCompareOp::Eq: walk ? b.Where(cols, ibComparisonType::ibComparisonType_Equal,    val)
		                                : b.Where(cols[0], ibComparisonType::ibComparisonType_Equal,    val); break;
		case ibQueryCompareOp::Ne: walk ? b.Where(cols, ibComparisonType::ibComparisonType_NotEqual, val)
		                                : b.Where(cols[0], ibComparisonType::ibComparisonType_NotEqual, val); break;
		case ibQueryCompareOp::Lt: walk ? b.WhereCompare(cols, ibQueryFilterOp::Less,         val) : b.WhereCompare(cols[0], ibQueryFilterOp::Less,         val); break;
		case ibQueryCompareOp::Le: walk ? b.WhereCompare(cols, ibQueryFilterOp::LessEqual,    val) : b.WhereCompare(cols[0], ibQueryFilterOp::LessEqual,    val); break;
		case ibQueryCompareOp::Gt: walk ? b.WhereCompare(cols, ibQueryFilterOp::Greater,      val) : b.WhereCompare(cols[0], ibQueryFilterOp::Greater,      val); break;
		case ibQueryCompareOp::Ge: walk ? b.WhereCompare(cols, ibQueryFilterOp::GreaterEqual, val) : b.WhereCompare(cols[0], ibQueryFilterOp::GreaterEqual, val); break;
		}
		return;
	}

	case ibQueryAstExprKind::Like: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		if (cols.size() > 1) b.WhereCompare(cols, ibQueryFilterOp::Like, val);
		else                 b.WhereLike(cols[0], val);
		return;
	}

	case ibQueryAstExprKind::Between: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue lo = EvalValue(*e.m_low, params), hi = EvalValue(*e.m_high, params);
		if (cols.size() > 1) {
			b.WhereCompare(cols, ibQueryFilterOp::GreaterEqual, lo);
			b.WhereCompare(cols, ibQueryFilterOp::LessEqual,    hi);
		} else {
			b.WhereCompare(cols[0], ibQueryFilterOp::GreaterEqual, lo);
			b.WhereCompare(cols[0], ibQueryFilterOp::LessEqual,    hi);
		}
		return;
	}

	default:
		Fail(e.m_line, e.m_col, _("this WHERE expression is not a flat condition"));
		return;
	}
}

// A WHERE is "flat" if it is a pure AND-tree of simple comparisons / LIKE / BETWEEN — no OR / NOT /
// IN / IS NULL. A flat JOIN WHERE rides the door's per-leaf verb conditions (work co-located AND in
// the RAM stitch); a boolean one goes through the predicate tree, which only the co-located join path
// lowers (per-leaf qualified) — the stitch path errors clearly rather than under-filter.
bool IsFlatAndWhere(const ibQueryAstExpr& e)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical: return !e.m_isOr && IsFlatAndWhere(*e.m_lhs) && IsFlatAndWhere(*e.m_rhs);
	case ibQueryAstExprKind::Compare: return true;
	case ibQueryAstExprKind::Like:    return !e.m_negated;
	case ibQueryAstExprKind::Between: return !e.m_negated;
	default:                       return false;   // Not / In / IsNull
	}
}

// Build an L3 computed-column expression (ibQueryColumnExpr) from an AST expression — arithmetic, CASE,
// a plain column, a literal, or a &parameter. The provider lowers it to the L2 IR and projects it AS an
// alias. Plain columns only (no dot-walk inside a computed expression yet). Single source.
ibQueryColumnExprPtr BuildColumnExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Column: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
		if (cols.size() != 1)
			Fail(e.m_line, e.m_col, _("a computed expression takes plain columns (no dot-walk)"));
		return ibQueryColumnExpr::Col(cols[0]);
	}
	case ibQueryAstExprKind::Literal:
		return ibQueryColumnExpr::Const(e.m_literal);
	case ibQueryAstExprKind::Param:
		return ibQueryColumnExpr::Const(EvalValue(e, params));

	case ibQueryAstExprKind::Arith: {
		const ibQueryColumnArithOp op =
			e.m_arith == ibQueryArithOp::Add ? ibQueryColumnArithOp::Add
			: e.m_arith == ibQueryArithOp::Sub ? ibQueryColumnArithOp::Sub
			: e.m_arith == ibQueryArithOp::Mul ? ibQueryColumnArithOp::Mul
			: e.m_arith == ibQueryArithOp::Div ? ibQueryColumnArithOp::Div
			                                   : ibQueryColumnArithOp::Mod;
		return ibQueryColumnExpr::Arith(op, BuildColumnExprFromAst(sources, *e.m_lhs, params),
		                                    BuildColumnExprFromAst(sources, *e.m_rhs, params));
	}

	case ibQueryAstExprKind::Case: {
		auto c = std::make_shared<ibQueryColumnExpr>();
		c->m_kind = ibQueryColumnExprKind::Case;
		for (const auto& wt : e.m_cases)
			c->m_cases.emplace_back(BuildWherePredicate(sources, *wt.first, params, /*allowDotWalk*/false),
			                        BuildColumnExprFromAst(sources, *wt.second, params));
		if (e.m_else)
			c->m_else = BuildColumnExprFromAst(sources, *e.m_else, params);
		return c;
	}

	default:
		Fail(e.m_line, e.m_col, _("unsupported expression in a computed column"));
		return nullptr;
	}
}

// A synthetic scalar column the totals lowering builds: it reads a value the door projected under a
// distinct cursor alias (a computed measure `1 AS test`; a dot-walk dimension's leaf `Parent.Code`),
// STRAIGHT off the cursor by that alias, under a UNIQUE synthetic model id. This is what lets the
// metaID-keyed totals fold read it as a normal column — AND keeps a self-referential dimension
// (Parent.Code, whose leaf shares a metaID with the row's own attribute) from clashing with the main
// table. The id sits in its own high range, clear of real metaIDs AND the COUNT(*) synthetic receivers
// (kAggSyntheticBase = 0x40000000).
const ibMetaID kSyntheticColumnBase = 0x50000000u;

class ibSyntheticScalarColumn : public ibRawDBColumn
{
public:
	ibSyntheticScalarColumn(const wxString& alias, ibMetaID id, RawType type = RawType::Number)
		: ibRawDBColumn(alias, type), m_id(id) {}
	ibMetaID GetColumnId() const override { return m_id; }
private:
	ibMetaID m_id;
};

// The raw read-type of a PLAIN SCALAR column (string / number / date / bool, single CLSID). Returns false
// for a reference / enum / composite leaf — those are not single-field scalars and cannot ride a synthetic
// raw column (a multi-type totals dimension is a separate feature).
bool ScalarRawType(const ibBackendQueryColumn* col, ibRawDBColumn::RawType& out)
{
	const ibTypeDescription& td = col->GetTypeDesc();
	if (td.GetClsidCount() != 1) return false;
	if      (td.ContainType(ibValueTypes::TYPE_STRING))  out = ibRawDBColumn::RawType::String;
	else if (td.ContainType(ibValueTypes::TYPE_NUMBER))  out = ibRawDBColumn::RawType::Number;
	else if (td.ContainType(ibValueTypes::TYPE_DATE))    out = ibRawDBColumn::RawType::Date;
	else if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) out = ibRawDBColumn::RawType::Boolean;
	else return false;
	return true;
}

wxString DeriveAlias(const ibQueryProjection& p, int idx)
{
	if (!p.m_alias.empty()) return p.m_alias;
	const ibQueryAstExpr& e = *p.m_expr;
	if (e.m_kind == ibQueryAstExprKind::Column && !e.m_path.empty())
		return e.m_path.back();
	if (e.m_kind == ibQueryAstExprKind::Func) {
		const wxString f = ibQueryKeywordText(e.m_func);
		if (e.m_star) return f + wxT("_all");
		return f + wxT("_") + (e.m_arg && !e.m_arg->m_path.empty() ? e.m_arg->m_path.back() : wxString());
	}
	return wxString::Format(wxT("col%d"), idx);
}

// ibSubqueryOwner owns the ibSubqueryQueryable instances built for a single Execute — they must outlive
// the door's terminal call (declared above for IN-subquery). RAM-materialised on Execute, so a local
// list living to the return statement is enough (no cross-call lifetime). (docs §22 / §23)
bool PopulateBuilder(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     const std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<OutputColumn>& outSchema, bool asSubquery);

// Resolve a FROM / source to a queryable: a plain metaobject source via the factory, or a nested
// SELECT wrapped in ibSubqueryQueryable (built recursively, its own FROM resolved the same way).
const ibBackendQueryable* ResolveFrom(const ibQuerySource& src,
                                      const std::map<wxString, ibValue>& params,
                                      ibSubqueryOwner& owner)
{
	if (!src.m_subquery)
		return ResolveSource(src, params);
	return WrapSelectAsQueryable(*src.m_subquery, params, owner);   // FROM (SELECT …) AS alias
}

// Populate the door from a single SELECT's clauses (projections / GROUP BY / HAVING / WHERE / ORDER /
// DISTINCT). Shared by the top-level execute, nested subqueries, and JOIN queries. The source set
// (1 = single source, >1 = JOIN) drives column resolution. explicitProjection (a subquery's inner
// query OR any multi-source query) PROJECTS plain columns via Select(col, alias) so the output schema
// is explicit; a plain single-source query instead records them in outSchema and reads the result
// column directly. Returns whether the query is in aggregate mode (GROUP BY / aggregate projection).
bool PopulateBuilder(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     const std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<OutputColumn>& outSchema, bool asSubquery)
{
	const bool multiSource      = sources.size() > 1;
	const bool explicitProjection = asSubquery || multiSource;
	// A COMPUTED primary (subquery / virtual table) materialises in RAM — reference dot-walk
	// joins and dot-walk aggregate inputs have no DB join to ride there; reject rather than
	// push a path leaf as a plain column (silently wrong rows).
	const bool computedPrimary  = sources.size() == 1 && sources[0].m_q != nullptr
	                              && sources[0].m_q->IsComputedInRam();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source projection)

	bool aggregate = !ast.m_groupBy.empty();
	for (const ibQueryProjection& p : ast.m_projections)
		if (p.m_expr && p.m_expr->m_kind == ibQueryAstExprKind::Func) aggregate = true;

	// projections -> output schema (+ door select for dot-walk / aggregates / explicit projection)
	outSchema.clear();
	if (ast.m_selectAll) {
		// SELECT * — every column of every source (a JOIN flattens all sides; a single source = its own).
		for (const ibSourceBinding& s : sources)
			for (const ibBackendQueryColumn* c : s.m_q->GetColumns()) {
				OutputColumn oc;
				oc.m_name = c->GetName();
				if (explicitProjection) { b.Select(c, c->GetName()); oc.m_alias = c->GetName(); oc.m_byAlias = true; }
				else                    { oc.m_col = c; }
				outSchema.push_back(oc);
			}
	}
	else {
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			const ibQueryAstExpr& e = *p.m_expr;
			const wxString alias = DeriveAlias(p, idx++);
			OutputColumn oc;
			oc.m_name = alias;

			if (e.m_kind == ibQueryAstExprKind::Func) {
				// Aggregate input: a plain column, a reference dot-walk leaf (SUM(Producer.Weight)), or a
				// COMPUTED expression (SUM(Qty * Price) — the provider lowers it; single DB source, gated).
				if (e.m_star) {
					b.Aggregate(AggFn(e.m_func), (const ibBackendQueryColumn*)nullptr, alias);
				}
				else if (e.m_arg && IsComputedExprAst(*e.m_arg)) {
					GateComputedExpr(sources, *e.m_arg);
					b.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), alias);
				}
				else {
					const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *e.m_arg);
					if (argCols.size() > 1 && computedPrimary)
						Fail(e.m_line, e.m_col, _("a dot-walk aggregate input over a computed source is not yet supported"));
					if (argCols.size() > 1 && multiSource) {
						// dot-walk aggregate input over a JOIN — expand the ref path into LEFT-join leaves and
						// aggregate the qualified leaf (same mechanism as the TOTALS dimension / projection).
						const ibBackendQueryColumn* dwLeaf =
							ExpandDotWalkJoins(b, RootForPath(sources, *e.m_arg), argCols, dwJoined, dwAliasSeq, *e.m_arg);
						b.Aggregate(AggFn(e.m_func), dwLeaf, alias);
					}
					else
						b.Aggregate(AggFn(e.m_func), argCols, alias);
				}
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else if (e.m_kind == ibQueryAstExprKind::Column) {
				const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, e);
				if (aggregate) {
					// AGGREGATE mode: a projected column is a GROUP BY key (a non-aggregate output must be
					// grouped). The provider GROUPS BY + projects it (plain or dot-walk leaf) and the result is
					// read back by the leaf column — NOT via SelectPath (the read-path machinery).
					if (pathCols.size() > 1 && computedPrimary)
						Fail(e.m_line, e.m_col, _("a dot-walk GROUP BY column over a computed source is not yet supported"));
					oc.m_col = (pathCols.size() > 1 && multiSource)
						? ExpandDotWalkJoins(b, RootForPath(sources, e), pathCols, dwJoined, dwAliasSeq, e)   // JOIN -> expand ref path
						: pathCols.back();
				}
				else if (pathCols.size() == 1 && !explicitProjection) {
					oc.m_col = pathCols[0];
				}
				else if (pathCols.size() == 1) {
					b.Select(pathCols[0], alias);   // explicit: project the plain column under its alias
					oc.m_alias = alias;
					oc.m_byAlias = true;
				}
				else if (multiSource) {
					// MULTI-SOURCE dot-walk projection — `SelectPath` is the single-source door's join; the RAM
					// stitch has none. Expand the path into explicit LEFT-join leaves (ExpandDotWalkJoins) and
					// project the qualified leaf column, read back by alias (a scalar value or a whole reference
					// cell). Paths sharing a prefix reuse one join via dwJoined.
					const ibBackendQueryColumn* dwLeaf =
						ExpandDotWalkJoins(b, RootForPath(sources, e), pathCols, dwJoined, dwAliasSeq, e);
					b.Select(dwLeaf, alias);
					oc.m_alias = alias;
					oc.m_byAlias = true;
				}
				else {
					if (computedPrimary)
						Fail(e.m_line, e.m_col, _("a dot-walk projection over a computed source (subquery / virtual table) is not yet supported"));
					b.SelectPath(pathCols, alias);
					oc.m_alias = alias;
					oc.m_byAlias = true;
					// A NON-scalar leaf (reference / enum / composite) is read by reassembling its full field
					// spread (the provider projects it under the alias prefix). A plain single-primitive scalar
					// keeps the by-alias single-field read. Same type test as the provider's scalar/object split.
					const ibTypeDescription& ltd = pathCols.back()->GetTypeDesc();
					const bool plainScalar = ltd.GetClsidCount() == 1
						&& (ltd.ContainType(ibValueTypes::TYPE_NUMBER) || ltd.ContainType(ibValueTypes::TYPE_STRING)
							|| ltd.ContainType(ibValueTypes::TYPE_DATE) || ltd.ContainType(ibValueTypes::TYPE_BOOLEAN));
					if (!plainScalar) {
						oc.m_objectPrefix = alias;
						oc.m_col          = pathCols.back();
					}
				}
			}
			else if (e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case) {
				// COMPUTED column (a * b, CASE …). The provider lowers the L3 expression tree + projects it
				// AS the alias. Single-source non-aggregate only (the read path BuildPageIR projects it).
				if (aggregate)   // single source -> SQL; JOIN -> composer RAM-eval; OVER aggregates only is unsupported
					Fail(e.m_line, e.m_col, _("a computed column (arithmetic / CASE) over aggregates is not supported"));
				if (computedPrimary)
					Fail(e.m_line, e.m_col, _("a computed column (arithmetic / CASE) over a computed source (subquery / virtual table) is not yet supported"));
				b.SelectExpr(BuildColumnExprFromAst(sources, e, params), alias);
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else {
				Fail(e.m_line, e.m_col, _("unsupported projection expression"));
			}
			outSchema.push_back(oc);
		}
	}

	// GROUP BY — plain column OR a reference dot-walk leaf (GROUP BY Producer.Region). The path overload
	// routes size-1 to a plain key; the provider joins a longer path (single source only — JOIN is A1).
	for (const ibQueryAstExprPtr& g : ast.m_groupBy) {
		const std::vector<const ibBackendQueryColumn*> gcols = ResolvePath(sources, *g);
		if (gcols.size() > 1 && computedPrimary)
			Fail(g->m_line, g->m_col, _("a dot-walk GROUP BY over a computed source is not yet supported"));
		if (gcols.size() > 1 && multiSource)
			b.GroupBy(ExpandDotWalkJoins(b, RootForPath(sources, *g), gcols, dwJoined, dwAliasSeq, *g));   // JOIN -> expand ref path
		else
			b.GroupBy(gcols);
	}

	// HAVING — aggregate <op> value (ordered ops only; the door's filter-op set has no = / <>)
	// Over a COMPUTED source the aggregate folds in RAM, which does not apply HAVING — reject
	// rather than silently return unfiltered groups.
	if (ast.m_having && sources.size() == 1 && sources[0].m_q != nullptr && sources[0].m_q->IsComputedInRam())
		Fail(ast.m_having->m_line, ast.m_having->m_col,
			_("HAVING over a computed source (subquery / virtual table) is not yet supported"));
	if (ast.m_having) {
		const ibQueryAstExpr& h = *ast.m_having;
		if (h.m_kind != ibQueryAstExprKind::Compare || h.m_lhs->m_kind != ibQueryAstExprKind::Func)
			Fail(h.m_line, h.m_col, _("HAVING must compare an aggregate function to a value"));
		const ibQueryAstExpr& f = *h.m_lhs;
		const ibBackendQueryColumn* col = f.m_star ? nullptr : ResolveColumnSingle(sources, *f.m_arg);
		const ibValue val = EvalValue(*h.m_rhs, params);
		ibQueryFilterOp op = ibQueryFilterOp::Greater;
		switch (h.m_cmp) {
		case ibQueryCompareOp::Lt: op = ibQueryFilterOp::Less;         break;
		case ibQueryCompareOp::Le: op = ibQueryFilterOp::LessEqual;    break;
		case ibQueryCompareOp::Gt: op = ibQueryFilterOp::Greater;      break;
		case ibQueryCompareOp::Ge: op = ibQueryFilterOp::GreaterEqual; break;
		default: Fail(h.m_line, h.m_col, _("HAVING supports only <, <=, >, >=")); break;
		}
		b.Having(AggFn(f.m_func), col, op, val);
	}

	// Dot-walk in WHERE / ORDER is realizable only on a single-source, non-aggregate, PHYSICAL READ
	// (the provider's BuildPageIR builds the reference join + qualifies the leaf). An aggregate /
	// JOIN / computed-source query rejects it (a computed source has no DB join to ride).
	const bool allowDotWalk = !aggregate && !multiSource && !computedPrimary;

	// WHERE — a FLAT AND-of-simple WHERE rides the door's verb conditions (plain columns + dot-walk
	// leaves); a BOOLEAN WHERE (OR / NOT / IN / IS NULL) goes through the predicate tree. The tree
	// supports the full boolean for a single source — INCLUDING dot-walk leaves (Compare/LIKE/BETWEEN)
	// when allowDotWalk; the provider joins them. For a JOIN the tree lowers only in the co-located
	// path (the stitch path errors), and a dot-walk leaf there is rejected (allowDotWalk is false).
	if (ast.m_where) {
		if (IsFlatAndWhere(*ast.m_where))
			LowerFlatWhere(b, sources, *ast.m_where, params, allowDotWalk);
		else
			b.Where(BuildWherePredicate(sources, *ast.m_where, params, allowDotWalk));
	}

	// ORDER BY — plain column or reference dot-walk leaf.
	for (const ibQueryOrderItem& o : ast.m_orderBy) {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *o.m_expr, allowDotWalk);
		if (cols.size() > 1) b.OrderBy(cols, o.m_ascending);
		else                 b.OrderBy(cols[0], o.m_ascending);
	}

	if (ast.m_distinct)
		b.Distinct();

	return aggregate;
}

// Build a SELECT's CORE (projections / FROM / WHERE / GROUP — NOT order/totals/unions) into an inner
// door and wrap it in ibSubqueryQueryable (owned in `owner`). Used for a subquery source AND for each
// branch of a UNION (the branch is itself a sub-SELECT). The wrapper exposes the branch's output
// columns (by their Select alias) — so the outer query / the UNION matches columns by name.
const ibBackendQueryable* WrapSelectAsQueryable(const ibQuerySelect& sel,
                                                const std::map<wxString, ibValue>& params,
                                                ibSubqueryOwner& owner)
{
	if (!sel.m_joins.empty() || sel.m_hasTotals)
		Fail(0, 0, _("a subquery / UNION branch may not use JOIN or TOTALS yet"));

	const ibBackendQueryable* qi = ResolveFrom(sel.m_from, params, owner);   // recurse — nested subqueries
	ibDataQueryBuilder inner;
	inner.From(qi, sel.m_from.m_alias);

	std::vector<OutputColumn> innerSchema;
	const std::vector<ibSourceBinding> innerSources{ { sel.m_from.m_alias, qi } };
	// An AGGREGATE inner (GROUP BY / aggregate projections) is fine: the wrapper detects it from the
	// builder, exposes the group keys + a synthetic column per aggregate alias, and ComputeRows runs
	// SelectAggregate. The outer's pushed-down conditions post-filter the materialised rows.
	PopulateBuilder(sel, params, innerSources, inner, innerSchema, /*asSubquery*/true);

	// ibSubqueryQueryable copies the inner door (shares its owned raw columns via shared_ptr), so the
	// local 'inner' may die here — the copy is self-sufficient. The wrapper itself lives in 'owner'.
	// sel.m_top (SELECT TOP n in the branch / subquery) limits the materialised rows.
	auto wrapped = std::unique_ptr<ibSubqueryQueryable>(new ibSubqueryQueryable(inner, sel.m_top));
	const ibBackendQueryable* result = wrapped.get();
	owner.push_back(std::move(wrapped));
	return result;
}

// UNION — every branch (the first SELECT's core + each m_unions branch) is wrapped as a queryable and
// stacked vertically; the trailing ORDER BY applies to the whole. The composer realizes the stack
// (RAM union today, co-located where possible). Columns match BY NAME across branches.
ibDataQueryResult LowerUnion(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                            std::vector<OutputColumn>& outSchema, ibSubqueryOwner& owner)
{
	// First branch = ast's CORE (strip the whole-union ORDER / TOTALS / the union list itself).
	// Its TOP is stripped too: on the first core it means the WHOLE-union limit (like the trailing
	// ORDER BY) and applies at the final Execute; a later branch's TOP limits that branch only.
	ibQuerySelect core0 = ast;
	core0.m_orderBy.clear();
	core0.m_unions.clear();
	core0.m_totalsBy.clear();
	core0.m_totalsAggregates.clear();
	core0.m_hasTotals = false;
	core0.m_top = 0;

	const ibBackendQueryable* b0 = WrapSelectAsQueryable(core0, params, owner);

	ibDataQueryBuilder b;
	b.From(b0);

	// The union's output = the first branch's columns (read back by name); each is the output schema.
	outSchema.clear();
	for (const ibBackendQueryColumn* c : b0->GetColumns()) {
		if (c == nullptr) continue;
		b.Select(c, c->GetName());
		OutputColumn oc; oc.m_name = c->GetName(); oc.m_alias = c->GetName(); oc.m_byAlias = true;
		outSchema.push_back(oc);
	}

	// Each branch carries its UNION-vs-ALL flag: plain UNION dedupes the accumulated rows at its
	// operator (SQL left-assoc semantics), UNION ALL keeps duplicates.
	for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
		b.Union(WrapSelectAsQueryable(*u, params, owner), wxEmptyString, /*keepDuplicates*/ u->m_unionAll);

	// ORDER BY on the whole union — resolve against the first branch's columns (by name).
	const std::vector<ibSourceBinding> usrc{ { wxEmptyString, b0 } };
	for (const ibQueryOrderItem& o : ast.m_orderBy)
		b.OrderBy(ResolveColumnSingle(usrc, *o.m_expr), o.m_ascending);

	ibReadPageRequest page;
	page.m_count = ast.m_top;   // TOP on the first core = the whole-union row limit (0 = all)
	return b.Execute(page);
}

} // namespace

//////////////////////////////////////////////////////////////////////
// L4-2 — recorded-lambda lowering (the Queryable fold reuses the same
// file-local builders the text language lowers through). Bail = empty,
// never a thrown user error: untranslatable folds fall back to RAM.
//////////////////////////////////////////////////////////////////////

ibQueryPredicatePtr ibQueryLowering::LowerLambdaPredicate(const ibBackendQueryable* source,
                                                          const ibQueryAstExpr& expr,
                                                          const std::map<wxString, ibValue>& captured)
{
	if (source == nullptr)
		return nullptr;
	const std::vector<ibSourceBinding> sources{ { wxEmptyString, source } };
	try {
		// Dot-walk leaves ride only on a physical single source (same gate as text).
		return BuildWherePredicate(sources, expr, captured, /*allowDotWalk*/ !source->IsComputedInRam());
	}
	catch (...) {
		return nullptr;   // resolution / subset failure -> the fold bails to RAM
	}
}

std::vector<const ibBackendQueryColumn*> ibQueryLowering::LowerLambdaColumnPath(
	const ibBackendQueryable* source, const ibQueryAstExpr& expr)
{
	if (source == nullptr || expr.m_kind != ibQueryAstExprKind::Column)
		return {};
	const std::vector<ibSourceBinding> sources{ { wxEmptyString, source } };
	try {
		std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, expr);
		if (cols.size() > 1 && source->IsComputedInRam())
			return {};   // dot-walk needs a physical source
		return cols;
	}
	catch (...) {
		return {};
	}
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::Execute
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema)
{
	return ExecuteImpl(astIn, params, outSchema, ibReadPageRequest{}, nullptr, wxEmptyString);
}

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema,
                                           const ibReadPageRequest& page)
{
	return ExecuteImpl(astIn, params, outSchema, page, nullptr, wxEmptyString);
}

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema,
                                           const ibReadPageRequest& page,
                                           ibRenderedPageCache& cache, const wxString& signature)
{
	return ExecuteImpl(astIn, params, outSchema, page, &cache, signature);
}

ibDataQueryResult ibQueryLowering::ExecuteImpl(const ibQuerySelect& astIn,
                                               const std::map<wxString, ibValue>& params,
                                               std::vector<OutputColumn>& outSchema,
                                               const ibReadPageRequest& pageIn,
                                               ibRenderedPageCache* cache, const wxString& signature)
{
	// Optimizer pass — negation normalization + FROM-subquery flattening. Works on a
	// deep clone; the Query value object's cached parse is never mutated. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	if (ast.m_hasTotals)
		Fail(0, 0, _("hierarchical TOTALS execution goes through ExecuteTotals, not Execute"));

	// Sources built for this run (subqueries / UNION branches) — must live until the door's terminal call.
	ibSubqueryOwner subOwners;

	// UNION — stack the branches vertically (the composer realizes it). The branch queryables live in
	// subOwners through the materialising terminal inside LowerUnion.
	if (!ast.m_unions.empty())
		return LowerUnion(ast, params, outSchema, subOwners);

	std::vector<ibSourceBinding> sources;
	const ibBackendQueryable* q0 = ResolveFrom(ast.m_from, params, subOwners);
	sources.push_back({ ast.m_from.m_alias, q0 });

	ibDataQueryBuilder b;
	b.From(q0, ast.m_from.m_alias);

	// JOINs — each adds a source; ON a.x = b.y joins on explicit columns (the provider qualifies each
	// key by its owning leaf, so the operand order is free), no ON = auto-join by reference.
	int refJoinSeq = 0;   // synthetic aliases for the intermediate segments of a named ref-join
	for (const ibQueryAstJoin& j : ast.m_joins) {
		const ibQueryJoinKind kind = MapJoinKind(j.m_kind);

		// Named ref-join: `JOIN rootAlias.refA[.refB…] AS alias` — the source is a REFERENCE PATH off an
		// existing source (not a metaobject), no ON. Auto-join the chain and bind the FINAL target to `alias`,
		// so a later `alias.field AS x` resolves as a clean qualified column (no ugly auto-name from a dotted
		// path). Sugar over ExpandDotWalkJoins; the first segment must be a live source alias.
		const ibBackendQueryable* refRoot = nullptr;
		if (!j.m_source.m_subquery && !j.m_on && j.m_source.m_name.size() >= 2
		    && (refRoot = SourceForAlias(sources, j.m_source.m_name[0])) != nullptr) {
			if (j.m_source.m_alias.empty())
				Fail(0, 0, _("a reference-path JOIN (alias.field) needs an explicit alias (AS)"));
			ExpandRefJoinAlias(b, sources, refRoot,
				std::vector<wxString>(j.m_source.m_name.begin() + 1, j.m_source.m_name.end()),
				j.m_source.m_alias, kind, refJoinSeq);
			continue;
		}

		const ibBackendQueryable* qi = ResolveFrom(j.m_source, params, subOwners);
		const wxString alias = j.m_source.m_alias;
		sources.push_back({ alias, qi });
		if (j.m_on && j.m_on->m_kind == ibQueryAstExprKind::Literal && j.m_on->m_literal.GetBoolean()) {
			b.CrossJoin(qi, kind, alias);   // ON TRUE -> cross join (cartesian)
		}
		else if (j.m_on) {
			if (j.m_on->m_kind != ibQueryAstExprKind::Compare)
				Fail(j.m_on->m_line, j.m_on->m_col, _("a JOIN ON clause must be a single comparison (a.x <op> b.y) or TRUE (cross join)"));
			const ibBackendQueryColumn* lc = ResolveColumnSingle(sources, *j.m_on->m_lhs);
			const ibBackendQueryColumn* rc = ResolveColumnSingle(sources, *j.m_on->m_rhs);
			b.Join(qi, lc, rc, MapJoinOp(j.m_on->m_cmp), kind, alias);   // = -> hash; <,<=,>,>=,<> -> RAM theta
		}
		else {
			b.Join(qi, kind, alias);   // auto-join by reference
		}
	}

	const bool aggregate = PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false);

	if (aggregate) {
		// SELECT TOP n + GROUP BY — the door's aggregate-terminal row limit: the DB / co-located
		// paths render the dialect LIMIT, the RAM fold truncates after grouping.
		if (ast.m_top > 0)
			b.Top(ast.m_top);
		return b.SelectAggregate();
	}

	// The external envelope drives the cursor; a `TOP n` in the text still caps the
	// page — the smaller positive count wins (0 = unbounded on either side). With a
	// caller-owned page cache the door reuses the rendered SQL, rebinding the anchor.
	ibReadPageRequest page = pageIn;
	if (ast.m_top > 0 && (page.m_count <= 0 || ast.m_top < page.m_count))
		page.m_count = ast.m_top;
	return cache != nullptr ? b.Execute(page, *cache, signature) : b.Execute(page);
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::ExecuteTotals — hierarchical subtotals (TOTALS … BY …)
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::ExecuteTotals(const ibQuerySelect& astIn,
                                                 const std::map<wxString, ibValue>& params,
                                                 std::vector<OutputColumn>& outSchema)
{
	// Same optimizer pass as Execute — the totals path benefits from a flattened FROM
	// and a normalized WHERE the same way. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	if (ast.m_totalsBy.empty())
		Fail(0, 0, _("TOTALS needs at least one BY dimension"));
	// TOP + TOTALS — the limit caps the DETAIL rows the fold runs over (the first n by ORDER BY), NOT the
	// subtotal tree. Applied as the page count on the detail read at the terminal below.

	ibSubqueryOwner owner;

	// FROM — single source, a JOIN chain, or a UNION stack. In every case the flat read
	// (b.Execute -> ExecuteRead) realizes the source (server-side or RAM-composed), the TotalBy config is
	// stamped on the result, and the runtime folds the ONE snapshot — no separate totals terminal. The
	// dimension / aggregate resolution below reads through `sources`. (docs/query-language-arc.md §22.1b)
	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;

	if (!ast.m_unions.empty()) {
		// UNION — stack the branches vertically (mirrors LowerUnion). The whole-union output = the FIRST
		// branch's columns (by name); dimensions / aggregates resolve against that branch, like the plain
		// union's trailing ORDER BY. The composer realizes the stack into one RAM snapshot the fold reads.
		ibQuerySelect core0 = ast;
		core0.m_orderBy.clear();
		core0.m_unions.clear();
		core0.m_totalsBy.clear();
		core0.m_totalsAggregates.clear();
		core0.m_hasTotals = false;
		core0.m_top = 0;

		const ibBackendQueryable* b0 = WrapSelectAsQueryable(core0, params, owner);
		b.From(b0);
		for (const ibBackendQueryColumn* c : b0->GetColumns())   // carry every union-output column into the snapshot
			if (c != nullptr) b.Select(c, c->GetName());
		for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
			b.Union(WrapSelectAsQueryable(*u, params, owner), wxEmptyString, /*keepDuplicates*/ u->m_unionAll);

		sources.push_back({ wxEmptyString, b0 });
	}
	else {
		// FROM + JOINs — same lowering as the non-totals path: ON a.x = b.y joins on explicit columns,
		// ON TRUE is a cross join, no ON is an auto-join by reference. The dimension / aggregate resolution
		// below reads through the multi-binding `sources`, so it qualifies a.col across leaves unchanged.
		const ibBackendQueryable* q = ResolveFrom(ast.m_from, params, owner);
		sources.push_back({ ast.m_from.m_alias, q });
		b.From(q, ast.m_from.m_alias);

		int refJoinSeq = 0;   // synthetic aliases for the intermediate segments of a named ref-join
		for (const ibQueryAstJoin& j : ast.m_joins) {
			// Named ref-join: `JOIN rootAlias.refA[.refB…] AS alias` — a reference PATH off an existing source,
			// no ON; auto-join the chain and bind the FINAL target to `alias` (sugar over ExpandDotWalkJoins).
			// The first segment must be a live source alias. (Mirrors the non-totals ExecuteImpl path.)
			const ibBackendQueryable* refRoot = nullptr;
			if (!j.m_source.m_subquery && !j.m_on && j.m_source.m_name.size() >= 2
			    && (refRoot = SourceForAlias(sources, j.m_source.m_name[0])) != nullptr) {
				if (j.m_source.m_alias.empty())
					Fail(0, 0, _("a reference-path JOIN (alias.field) needs an explicit alias (AS)"));
				ExpandRefJoinAlias(b, sources, refRoot,
					std::vector<wxString>(j.m_source.m_name.begin() + 1, j.m_source.m_name.end()),
					j.m_source.m_alias, MapJoinKind(j.m_kind), refJoinSeq);
				continue;
			}
			const ibBackendQueryable* qi = ResolveFrom(j.m_source, params, owner);
			const wxString alias = j.m_source.m_alias;
			sources.push_back({ alias, qi });

			const ibQueryJoinKind kind =
				  j.m_kind == ibQueryJoinKindAst::Left  ? ibQueryJoinKind::Left
				: j.m_kind == ibQueryJoinKindAst::Right ? ibQueryJoinKind::Right
				: j.m_kind == ibQueryJoinKindAst::Full  ? ibQueryJoinKind::Full
				                                        : ibQueryJoinKind::Inner;
			if (j.m_on && j.m_on->m_kind == ibQueryAstExprKind::Literal && j.m_on->m_literal.GetBoolean()) {
				b.CrossJoin(qi, kind, alias);   // ON TRUE -> cross join (cartesian)
			}
			else if (j.m_on) {
				if (j.m_on->m_kind != ibQueryAstExprKind::Compare)
					Fail(j.m_on->m_line, j.m_on->m_col, _("a JOIN ON clause must be a single comparison (a.x <op> b.y) or TRUE (cross join)"));
				const ibBackendQueryColumn* lc = ResolveColumnSingle(sources, *j.m_on->m_lhs);
				const ibBackendQueryColumn* rc = ResolveColumnSingle(sources, *j.m_on->m_rhs);
				b.Join(qi, lc, rc, MapJoinOp(j.m_on->m_cmp), kind, alias);   // = -> hash; <,<=,>,>=,<> -> RAM theta
			}
			else {
				b.Join(qi, kind, alias);   // auto-join by reference
			}
		}
	}

	outSchema.clear();
	ibMetaID nextSynthId = kSyntheticColumnBase;   // shared id pool for synthetic dimension + measure columns
	const bool multiSource = !ast.m_joins.empty() || !ast.m_unions.empty();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source)

	// The dimension levels, IN ORDER (each yields a subtotal node; the root is the grand total). They
	// are the leading output columns (their group key at each node — read by GetValue(col)).
	for (const ibQueryTotalDim& d : ast.m_totalsBy) {
		const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *d.m_expr);   // plain col OR dot-walk path
		const ibBackendQueryColumn* leaf = pathCols.back();
		const ibDimensionKind dim =
			d.m_unfold == ibQueryDimUnfold::Hierarchy       ? ibDimensionKind::Hierarchy
			: d.m_unfold == ibQueryDimUnfold::HierarchyOnly ? ibDimensionKind::HierarchyOnly
			                                                : ibDimensionKind::Elements;

		OutputColumn oc; oc.m_name = leaf->GetName();
		if (pathCols.size() == 1) {
			b.TotalBy(leaf, dim);            // plain dimension — group by the column's own metaID
			oc.m_col = leaf;
		}
		else {
			// DOT-WALK dimension — two strategies by source shape / leaf kind:
			//  - single-source SCALAR leaf (Parent.Code): SQL ROLLUP via a synthetic scalar projection
			//    (TotalByDotWalk) — the DBMS folds, efficient. The synthetic's DISTINCT id avoids a
			//    self-reference metaID clash with the main table's same-named field.
			//  - multi-source OR a NON-scalar leaf (reference / composite): expand the ref path into explicit
			//    LEFT-join leaves (ExpandDotWalkJoins) and group by the leaf in the RAM fold (by the leaf's
			//    VALUE — scalar OR reference). A non-scalar single-source leaf rides this too: adding the
			//    ref-join makes it multi-source / RAM-folded, grouping by the reference value the scalar
			//    synthetic could not carry. (A composite MID-segment still fails inside the expand — that path
			//    is not a single-target reference; same edge as projection.)
			ibRawDBColumn::RawType rt;
			const bool scalarLeaf = ScalarRawType(leaf, rt);
			if (multiSource || !scalarLeaf) {
				const ibBackendQueryColumn* dwLeaf =
					ExpandDotWalkJoins(b, RootForPath(sources, *d.m_expr), pathCols, dwJoined, dwAliasSeq, *d.m_expr);
				b.TotalBy(dwLeaf, dim);
				oc.m_col = dwLeaf;
			}
			else {
				const wxString alias = wxString::Format(wxT("dim%u"), static_cast<unsigned>(nextSynthId - kSyntheticColumnBase));
				auto synth = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++, rt);
				b.TotalByDotWalk(pathCols, synth.get(), alias, dim);   // provider joins path, projects leaf scalar AS alias
				oc.m_col = synth.get(); oc.m_ownedCol = synth;
			}
		}
		outSchema.push_back(oc);
	}

	// SELECT output-name map, so a TOTALS aggregate may name a SELECTed field (the resource pattern:
	// SELECT Price … TOTALS SUM(Price); SELECT 1 AS test … TOTALS SUM(test)). A real column aggregates by
	// its metaID; a COMPUTED / constant field is projected by the door and aggregated through a SYNTHETIC
	// measure column (below) — both readable by the metaID-keyed totals fold.
	std::map<wxString, const ibQueryProjection*> selectByName;
	{
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr) continue;
			selectByName[DeriveAlias(p, idx++)] = &p;
		}
	}

	// The TOTALS aggregate set is COMMON across all dimension levels (each level rolls them IN-PLACE, so
	// the aggregate reads back off its own column — GetValue(col), same as a dimension).
	b.Totals();
	std::map<wxString, const ibBackendQueryColumn*> measureCol;   // computed alias -> its synthetic measure (projected once)

	for (const ibQueryAstExprPtr& agg : ast.m_totalsAggregates) {
		if (!agg || agg->m_kind != ibQueryAstExprKind::Func)
			Fail(0, 0, _("TOTALS expects aggregate functions (SUM/COUNT/MIN/MAX/AVG)"));

		// Output name read back via res[name]: COUNT(*) -> the function name; a field -> its identifier.
		const wxString outName = agg->m_star
			? ibQueryKeywordText(agg->m_func)
			: (agg->m_arg && !agg->m_arg->m_path.empty() ? agg->m_arg->m_path.back() : ibQueryKeywordText(agg->m_func));

		const ibBackendQueryColumn*           col   = nullptr;   // the column the fold aggregates by metaID
		std::shared_ptr<ibBackendQueryColumn> owned;             // set only for a synthetic computed measure
		if (!agg->m_star) {
			// A bare identifier may name a SELECTed field (alias) before a metadata attribute.
			const bool bareName = agg->m_arg->m_kind == ibQueryAstExprKind::Column && agg->m_arg->m_path.size() == 1;
			auto pit = bareName ? selectByName.find(agg->m_arg->m_path.back()) : selectByName.end();

			if (pit != selectByName.end() && pit->second->m_expr->m_kind != ibQueryAstExprKind::Column) {
				// COMPUTED / constant SELECT field — project the expression once, aggregate it through a
				// synthetic measure column that reads the projected field (unique id for the metaID fold).
				const wxString alias = pit->first;
				auto mit = measureCol.find(alias);
				if (mit != measureCol.end())
					col = mit->second;   // already projected for an earlier aggregate over the same field
				else {
					b.SelectExpr(BuildColumnExprFromAst(sources, *pit->second->m_expr, params), alias);
					owned = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++);   // RawType::Number measure
					col   = owned.get();
					measureCol[alias] = col;
				}
			}
			else if (pit != selectByName.end()) {
				col = ResolvePath(sources, *pit->second->m_expr).back();   // a SELECTed real column, named by alias
			}
			else col = ResolveColumnSingle(sources, *agg->m_arg);          // a metadata attribute
		}
		b.Aggregate(AggFn(agg->m_func), col, outName);   // in-place — rolls into its own column (named for read-back)

		OutputColumn oc; oc.m_name = outName;
		if (col != nullptr) { oc.m_col = col; oc.m_ownedCol = owned; }   // real OR synthetic column — keyed by metaID
		else { oc.m_alias = outName; oc.m_byAlias = true; }              // COUNT(*) — synthetic receiver, read by name
		outSchema.push_back(oc);
	}

	// WHERE (flat verbs or the boolean tree) — dot-walk rejected here (the totals fold is its own path).
	if (ast.m_where) {
		if (IsFlatAndWhere(*ast.m_where))
			LowerFlatWhere(b, sources, *ast.m_where, params, /*allowDotWalk*/false);
		else
			b.Where(BuildWherePredicate(sources, *ast.m_where, params, /*allowDotWalk*/false));
	}

	// One read → one snapshot, with the TotalBy config STAMPED on the result; the runtime's
	// QueryResult.Select() folds it (ByGroupsHierarchy) — no second query, so detail and subtotal
	// cannot skew. The synthetic measure columns live in outSchema (m_ownedCol), which travels with the
	// selection — so the result's stamped raw pointers into them stay valid through Select(). (docs §22.1b)
	//
	// TOP n caps the DETAIL rows the fold runs over — the first n (0 = all); the subtotals then roll over
	// exactly those rows. The tree itself is not row-limited (a subtotal is not a detail row).
	ibReadPageRequest page;
	if (ast.m_top > 0) page.m_count = ast.m_top;
	return b.Execute(page);
}
