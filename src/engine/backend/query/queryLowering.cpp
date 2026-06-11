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
		// `WHERE Поле.Продавец`  ==  `WHERE Поле.Продавец = TRUE`. (`= ИСТИНА` lowers as a plain Compare above.)
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
	ibMetaID GetModelID() const override { return m_id; }
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
				// Aggregate input: a plain column OR a reference dot-walk leaf (SUM(Producer.Weight)). The
				// dot-walk path overload routes size-1 to the plain aggregate; the provider joins a longer path.
				if (e.m_star) {
					b.Aggregate(AggFn(e.m_func), (const ibBackendQueryColumn*)nullptr, alias);
				}
				else {
					const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *e.m_arg);
					if (argCols.size() > 1 && multiSource)
						Fail(e.m_line, e.m_col, _("a dot-walk aggregate input over a JOIN is not yet supported"));
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
					if (pathCols.size() > 1 && multiSource)
						Fail(e.m_line, e.m_col, _("a dot-walk GROUP BY column over a JOIN is not yet supported"));
					oc.m_col = pathCols.back();
				}
				else if (pathCols.size() == 1 && !explicitProjection) {
					oc.m_col = pathCols[0];
				}
				else if (pathCols.size() == 1) {
					b.Select(pathCols[0], alias);   // explicit: project the plain column under its alias
					oc.m_alias = alias;
					oc.m_byAlias = true;
				}
				else {
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
		if (gcols.size() > 1 && multiSource)
			Fail(g->m_line, g->m_col, _("a dot-walk GROUP BY over a JOIN is not yet supported"));
		b.GroupBy(gcols);
	}

	// HAVING — aggregate <op> value (ordered ops only; the door's filter-op set has no = / <>)
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

	// Dot-walk in WHERE / ORDER is realizable only on a single-source, non-aggregate READ (the provider's
	// BuildPageIR builds the reference join + qualifies the leaf). An aggregate / JOIN query rejects it.
	const bool allowDotWalk = !aggregate && !multiSource;

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
	if (PopulateBuilder(sel, params, innerSources, inner, innerSchema, /*asSubquery*/true))
		Fail(0, 0, _("aggregate subqueries / UNION branches are not yet supported"));

	// ibSubqueryQueryable copies the inner door (shares its owned raw columns via shared_ptr), so the
	// local 'inner' may die here — the copy is self-sufficient. The wrapper itself lives in 'owner'.
	auto wrapped = std::unique_ptr<ibSubqueryQueryable>(new ibSubqueryQueryable(inner));
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
	ibQuerySelect core0 = ast;
	core0.m_orderBy.clear();
	core0.m_unions.clear();
	core0.m_totalsBy.clear();
	core0.m_totalsAggregates.clear();
	core0.m_hasTotals = false;

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

	for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
		b.Union(WrapSelectAsQueryable(*u, params, owner));

	// ORDER BY on the whole union — resolve against the first branch's columns (by name).
	const std::vector<ibSourceBinding> usrc{ { wxEmptyString, b0 } };
	for (const ibQueryOrderItem& o : ast.m_orderBy)
		b.OrderBy(ResolveColumnSingle(usrc, *o.m_expr), o.m_ascending);

	return b.Execute(ibReadPageRequest{});
}

} // namespace

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::Execute
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema)
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
	for (const ibQueryAstJoin& j : ast.m_joins) {
		const ibBackendQueryable* qi = ResolveFrom(j.m_source, params, subOwners);
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
			if (j.m_on->m_kind != ibQueryAstExprKind::Compare || j.m_on->m_cmp != ibQueryCompareOp::Eq)
				Fail(j.m_on->m_line, j.m_on->m_col, _("a JOIN ON clause must be a single equality (a.x = b.y) or TRUE (cross join)"));
			const ibBackendQueryColumn* lc = ResolveColumnSingle(sources, *j.m_on->m_lhs);
			const ibBackendQueryColumn* rc = ResolveColumnSingle(sources, *j.m_on->m_rhs);
			b.Join(qi, lc, rc, kind, alias);
		}
		else {
			b.Join(qi, kind, alias);   // auto-join by reference
		}
	}

	const bool aggregate = PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false);

	return aggregate ? b.SelectAggregate() : b.Execute(ibReadPageRequest{});
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::ExecuteTotals — hierarchical subtotals (ИТОГИ … ПО …)
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::ExecuteTotals(const ibQuerySelect& astIn,
                                                 const std::map<wxString, ibValue>& params,
                                                 std::vector<OutputColumn>& outSchema)
{
	// Same optimizer pass as Execute — the totals path benefits from a flattened FROM
	// and a normalized WHERE the same way. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	if (!ast.m_joins.empty() || !ast.m_unions.empty())
		Fail(0, 0, _("TOTALS over a JOIN / UNION is not yet supported (use a single source)"));
	if (ast.m_totalsBy.empty())
		Fail(0, 0, _("TOTALS needs at least one BY dimension"));

	ibSubqueryOwner owner;
	const ibBackendQueryable* q = ResolveFrom(ast.m_from, params, owner);
	const std::vector<ibSourceBinding> sources{ { ast.m_from.m_alias, q } };

	ibDataQueryBuilder b;
	b.From(q, ast.m_from.m_alias);

	outSchema.clear();
	ibMetaID nextSynthId = kSyntheticColumnBase;   // shared id pool for synthetic dimension + measure columns

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
			// DOT-WALK dimension (Parent.Code): a self-referential leaf shares a metaID with the row's own
			// attribute, so the provider projects it under a DISTINCT alias and the fold groups by a SYNTHETIC
			// column's unique id — no clash with the main table's same-metaID column. Scalar leaves only.
			ibRawDBColumn::RawType rt;
			if (!ScalarRawType(leaf, rt))
				Fail(d.m_expr->m_line, d.m_expr->m_col,
					_("TOTALS BY a reference / composite dot-walk leaf is not supported yet (use a scalar attribute)"));
			const wxString alias = wxString::Format(wxT("dim%u"), static_cast<unsigned>(nextSynthId - kSyntheticColumnBase));
			auto synth = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++, rt);
			b.TotalByDotWalk(pathCols, synth.get(), alias, dim);   // provider joins path, projects leaf scalar AS alias
			oc.m_col = synth.get(); oc.m_ownedCol = synth;
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
	return b.Execute(ibReadPageRequest{});
}
