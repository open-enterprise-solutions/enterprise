////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — AST back to query text (the return trip)
////////////////////////////////////////////////////////////////////////////

#include "queryRender.h"

#include "core/query/queryKeywords.h"
#include "core/localization/dictionary.h"   // _() — the renderer's own refusals are translated

#include <set>   // which levels open a branch — read while the BY list is joined


namespace {

// Every keyword comes from the ACTIVE table, never from a literal here — that is
// what keeps a localized table rendering in the language it parses.
ibString Kw(ibQueryKeyword kw)
{
	return ibQueryKeywordText(kw);
}

ibString RenderExpr(const ibQueryAstExpr& expr);
ibString RenderSelect(const ibQuerySelect& select, int indent);

// ⭐ A NESTED QUERY IS SHIFTED ONE STEP. Rendered flush against its parent it reads as a second
// query that happens to be inside a bracket; shifted, the nesting is visible at a glance — which is
// the only thing the reader of a generated query has to go on. One step, so depth stays legible
// rather than marching off the right edge.
constexpr int kNestedIndent = 4;

ibString Join(const std::vector<ibString>& parts, const ibString& separator)
{
	ibString out;
	for (std::size_t i = 0; i < parts.size(); i++) {
		if (i > 0) out += separator;
		out += parts[i];
	}
	return out;
}

// A LITERAL, written the way the lexer reads it back.
//
// Strings are quoted and their quotes doubled; dates use the language's own
// literal form; numbers print without trailing noise. Anything else is rendered
// through the value's own string form — which is the honest answer for a type
// the query language has no literal syntax for, and the parser will then refuse
// it rather than silently accept something else.
ibString RenderLiteral(const ibVariant& value)
{
	switch (value.GetType())
	{
	case ibValueTypes::TYPE_STRING:
	{
		ibString text = value.GetString();
		text.Replace(ibT("\""), ibT("\"\""));
		return ibT("\"") + text + ibT("\"");
	}
	case ibValueTypes::TYPE_BOOLEAN:
		return value.GetBoolean() ? Kw(ibQueryKeyword::True) : Kw(ibQueryKeyword::False);
	case ibValueTypes::TYPE_NULL:
		return Kw(ibQueryKeyword::Null);
	case ibValueTypes::TYPE_DATE:
	{
		ibDateTimeParts date;
		value.GetDate().ToParts(date);
		// DATETIME(y, m, d[, h, mi, s]) — the form the lexer parses. The time half
		// is written only when it carries something, so a plain date stays plain.
		if (date.m_hour == 0 && date.m_minute == 0 && date.m_second == 0) {
			return ibString::Format(ibT("DATETIME(%d, %u, %u)"),
				date.m_year, date.m_month, date.m_day);
		}
		return ibString::Format(ibT("DATETIME(%d, %u, %u, %u, %u, %u)"),
			date.m_year, date.m_month, date.m_day,
			date.m_hour, date.m_minute, date.m_second);
	}
	// ⭐ THE RUNTIME'S EMPTY VALUE, WRITTEN AS ITSELF. It used to be printed as `NULL`, which is the
	// database's absence and a different thing — and printing it so made a round trip lose it: the text
	// came back as a NULL literal, and a query rebuilt from its own rendering no longer asked what it had
	// asked. Now the language has the word (queryKeywords.h), so the rendering has it too.
	case ibValueTypes::TYPE_EMPTY:
		return Kw(ibQueryKeyword::Undefined);
	default:
		return value.GetString();
	}
}

ibString CompareOpText(ibQueryCompareOp op)
{
	switch (op)
	{
	case ibQueryCompareOp::Eq: return ibT("=");
	case ibQueryCompareOp::Ne: return ibT("<>");
	case ibQueryCompareOp::Lt: return ibT("<");
	case ibQueryCompareOp::Le: return ibT("<=");
	case ibQueryCompareOp::Gt: return ibT(">");
	case ibQueryCompareOp::Ge: return ibT(">=");
	}
	return ibT("=");
}

ibString ArithOpText(ibQueryArithOp op)
{
	switch (op)
	{
	case ibQueryArithOp::Add: return ibT("+");
	case ibQueryArithOp::Sub: return ibT("-");
	case ibQueryArithOp::Mul: return ibT("*");
	case ibQueryArithOp::Div: return ibT("/");
	case ibQueryArithOp::Mod: return ibT("%");
	}
	return ibT("+");
}

// PARENTHESISED WHEN IT COULD BIND WRONG.
//
// The AST has no parentheses — the author's are consumed by the parser and the
// structure is what remains. Rendering without them would re-associate on the
// next parse: `a AND (b OR c)` would come back as `a AND b OR c`, which is a
// different query and a silent one.
//
// So a compound child is wrapped whenever it sits inside something that binds
// tighter. Wrapping a little more than strictly necessary is deliberate: an
// extra pair of parentheses changes nothing about meaning, while a missing pair
// changes everything.
bool NeedsParens(const ibQueryAstExpr& child)
{
	switch (child.m_kind)
	{
	case ibQueryAstExprKind::Logical:
	case ibQueryAstExprKind::Compare:
	case ibQueryAstExprKind::Like:
	case ibQueryAstExprKind::In:
	case ibQueryAstExprKind::IsNull:
	case ibQueryAstExprKind::Between:
	case ibQueryAstExprKind::Refs:     // `(x REFS Catalog.Goods) = TRUE` — without them the `=` binds to the type name
	case ibQueryAstExprKind::Arith:
	case ibQueryAstExprKind::Not:
		return true;
	default:
		return false;
	}
}

ibString RenderChild(const ibQueryAstExprPtr& child)
{
	if (!child)
		return ibEmptyString;
	const ibString text = RenderExpr(*child);
	return NeedsParens(*child) ? ibT("(") + text + ibT(")") : text;
}

ibString RenderExpr(const ibQueryAstExpr& expr)
{
	switch (expr.m_kind)
	{
	case ibQueryAstExprKind::Column:
		// A WALK ROOTED ON A CAST writes its root first: `CAST(x AS T).A.B`. The root is in m_arg for
		// exactly this shape and for nothing else, so a plain column is unaffected.
		if (expr.m_arg && expr.m_arg->m_kind == ibQueryAstExprKind::Cast)
			return RenderExpr(*expr.m_arg) + ibT(".") + Join(expr.m_path, ibT("."));
		return Join(expr.m_path, ibT("."));

	case ibQueryAstExprKind::Literal:
		return RenderLiteral(expr.m_literal);

	case ibQueryAstExprKind::Param:
		return ibT("&") + expr.m_paramName;

	case ibQueryAstExprKind::Value:
		return Kw(ibQueryKeyword::Value) + ibT("(") + Join(expr.m_path, ibT(".")) + ibT(")");

	case ibQueryAstExprKind::Cast:
		return Kw(ibQueryKeyword::Cast) + ibT("(") + (expr.m_arg ? RenderExpr(*expr.m_arg) : ibString())
			+ ibT(" ") + Kw(ibQueryKeyword::As) + ibT(" ") + ibRenderQueryCastTarget(expr) + ibT(")");

	// ⭐ A SCALAR CALL, WRITTEN BACK AS IT WAS READ. This renderer feeds a PARSER — the constructor's
	// tabs round-trip every expression through it — so a kind it does not know does not merely look
	// wrong, it VANISHES: the query came back with `AS Y` and nothing in front of it (measured
	// 2026-09-05, in the journal's own echo of the text). The name comes from the table that declares
	// the call, so a call added to the language renders the day it is added.
	case ibQueryAstExprKind::ScalarCall:
	{
		ibString out = ibQueryScalarFnText(expr.m_scalar) + ibT("(");
		for (size_t i = 0; i < expr.m_args.size(); ++i) {
			if (i) out += ibT(", ");
			if (expr.m_args[i]) out += RenderExpr(*expr.m_args[i]);
		}
		return out + ibT(")");
	}

	// `<expr> [NOT] REFS <Kind>.<Name>` — the operator's own shape, negation included.
	case ibQueryAstExprKind::Refs:
		return (expr.m_lhs ? RenderExpr(*expr.m_lhs) : ibString())
			+ (expr.m_negated ? ibT(" ") + Kw(ibQueryKeyword::Not) : ibString())
			+ ibT(" ") + Kw(ibQueryKeyword::Refs) + ibT(" ") + Join(expr.m_path, ibT("."));

	case ibQueryAstExprKind::Func:
	{
		// A RANKING call has no argument at all — `ROW_NUMBER()`, not `ROW_NUMBER(*)`. Rendering the
		// star here would produce text the parser refuses, which is the worst kind of round trip: the
		// query survives being read and dies being read BACK.
		const bool ranking = ibIsRankingKeyword(expr.m_func);
		const ibString arg = ranking ? ibString()
			: (expr.m_star ? ibString(ibT("*")) : RenderExpr(*expr.m_arg));
		const ibString distinct = expr.m_distinctArg ? Kw(ibQueryKeyword::Distinct) + ibT(" ") : ibString();

		ibString out = Kw(expr.m_func) + ibT("(") + distinct + arg + ibT(")");

		// …and the window, when the call carries one. Written back exactly as it is read — the whole
		// point of a renderer that feeds a PARSER (the constructor's nine tabs round-trip through it).
		if (expr.m_over) {
			out += ibT(" ") + Kw(ibQueryKeyword::Over) + ibT(" (");
			bool first = true;
			if (!expr.m_over->m_partitionBy.empty()) {
				out += Kw(ibQueryKeyword::Partition) + ibT(" ") + Kw(ibQueryKeyword::By) + ibT(" ");
				for (size_t i = 0; i < expr.m_over->m_partitionBy.size(); ++i) {
					if (i) out += ibT(", ");
					out += RenderExpr(*expr.m_over->m_partitionBy[i]);
				}
				first = false;
			}
			if (!expr.m_over->m_orderBy.empty()) {
				if (!first) out += ibT(" ");
				out += Kw(ibQueryKeyword::Order) + ibT(" ") + Kw(ibQueryKeyword::By) + ibT(" ");
				for (size_t i = 0; i < expr.m_over->m_orderBy.size(); ++i) {
					if (i) out += ibT(", ");
					out += RenderExpr(*expr.m_over->m_orderBy[i].m_expr);
					if (!expr.m_over->m_orderBy[i].m_ascending)
						out += ibT(" ") + Kw(ibQueryKeyword::Desc);
				}
				first = false;
			}
			if (expr.m_over->m_frame == ibQueryAstFrame::Rows)
				out += (first ? ibString() : ibString(ibT(" "))) + Kw(ibQueryKeyword::Rows);
			else if (expr.m_over->m_frame == ibQueryAstFrame::Range)
				out += (first ? ibString() : ibString(ibT(" "))) + Kw(ibQueryKeyword::Range);
			out += ibT(")");
		}
		return out;
	}

	case ibQueryAstExprKind::Arith:
		return RenderChild(expr.m_lhs) + ibT(" ") + ArithOpText(expr.m_arith)
			+ ibT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Compare:
		return RenderChild(expr.m_lhs) + ibT(" ") + CompareOpText(expr.m_cmp)
			+ ibT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Like:
		return RenderChild(expr.m_lhs)
			+ (expr.m_negated ? ibT(" ") + Kw(ibQueryKeyword::Not) : ibString())
			+ ibT(" ") + Kw(ibQueryKeyword::Like) + ibT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::In:
	{
		ibString out = RenderChild(expr.m_lhs);
		if (expr.m_negated) out += ibT(" ") + Kw(ibQueryKeyword::Not);
		out += ibT(" ") + Kw(ibQueryKeyword::In);
		// The unfold word, when there is one. It is part of the OPERATOR, so it is written where the
		// operator is — between IN and its operand — and a query that had it comes back with it.
		if (expr.m_unfold == ibQueryDimUnfold::Hierarchy)
			out += ibT(" ") + Kw(ibQueryKeyword::Hierarchy);
		else if (expr.m_unfold == ibQueryDimUnfold::HierarchyOnly)
			out += ibT(" ") + Kw(ibQueryKeyword::HierarchyOnly);
		out += ibT(" (");
		if (expr.m_subquery) {
			// An IN subquery is a nested query like any other — shifted so the bracket's contents do
			// not read as a continuation of the condition.
			out += ibT("\n") + RenderSelect(*expr.m_subquery, kNestedIndent) + ibT("\n");
		}
		else {
			std::vector<ibString> items;
			items.reserve(expr.m_list.size());
			for (const auto& item : expr.m_list)
				if (item) items.push_back(RenderExpr(*item));
			out += Join(items, ibT(", "));
		}
		return out + ibT(")");
	}

	case ibQueryAstExprKind::IsNull:
		return RenderChild(expr.m_lhs) + ibT(" ") + Kw(ibQueryKeyword::Is)
			+ (expr.m_negated ? ibT(" ") + Kw(ibQueryKeyword::Not) : ibString())
			+ ibT(" ") + Kw(ibQueryKeyword::Null);

	case ibQueryAstExprKind::Between:
		return RenderChild(expr.m_lhs)
			+ (expr.m_negated ? ibT(" ") + Kw(ibQueryKeyword::Not) : ibString())
			+ ibT(" ") + Kw(ibQueryKeyword::Between) + ibT(" ") + RenderChild(expr.m_low)
			+ ibT(" ") + Kw(ibQueryKeyword::And) + ibT(" ") + RenderChild(expr.m_high);

	case ibQueryAstExprKind::Logical:
		return RenderChild(expr.m_lhs)
			+ ibT(" ") + Kw(expr.m_isOr ? ibQueryKeyword::Or : ibQueryKeyword::And) + ibT(" ")
			+ RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Not:
		return Kw(ibQueryKeyword::Not) + ibT(" ") + RenderChild(expr.m_lhs);

	case ibQueryAstExprKind::Case:
	{
		// ⚠ WHAT A BRANCH GIVES BACK IS READ AS A VALUE — the parser takes THEN and ELSE at the
		// arithmetic level, so a CONDITION there has to come bracketed: `THEN (Qty > 0)`. Written bare,
		// `THEN Qty > 0` read as `THEN Qty` and died on the `>` with "expected END". Arithmetic needs no
		// bracket there and gets none, so every query that already round-tripped still reads the same.
		const auto branchValue = [](const ibQueryAstExpr& value) {
			return NeedsParens(value) && value.m_kind != ibQueryAstExprKind::Arith
				? ibT("(") + RenderExpr(value) + ibT(")")
				: RenderExpr(value);
		};
		ibString out = Kw(ibQueryKeyword::Case);
		for (const auto& branch : expr.m_cases) {
			out += ibT(" ") + Kw(ibQueryKeyword::When) + ibT(" ") + RenderExpr(*branch.first);
			out += ibT(" ") + Kw(ibQueryKeyword::Then) + ibT(" ") + branchValue(*branch.second);
		}
		if (expr.m_else)
			out += ibT(" ") + Kw(ibQueryKeyword::Else) + ibT(" ") + branchValue(*expr.m_else);
		return out + ibT(" ") + Kw(ibQueryKeyword::End);
	}
	}

	return ibEmptyString;
}

ibString RenderSource(const ibQuerySource& source, int indent)
{
	ibString out;
	if (source.m_subquery) {
		// The brackets stay on the parent's line; everything between them belongs to the child, so
		// it is rendered at the child's own indent and closes back at the parent's.
		const ibString pad(ibT(' '), indent);
		out = ibT("(\n") + RenderSelect(*source.m_subquery, indent + kNestedIndent)
		    + ibT("\n") + pad + ibT(")");
	}
	else {
		// The `&` goes back on: what it writes, the parser reads — and a table handed in from
		// outside has to still say so after a round trip.
		out = (source.m_parameter ? ibT("&") : ibT("")) + Join(source.m_name, ibT("."));
		if (!source.m_args.empty()) {
			// ⚠ AN OMITTED ARGUMENT KEEPS ITS PLACE. The arguments are POSITIONAL — the source reads
			// the second one as its condition whatever the first one is — so skipping an empty one
			// while writing would slide every later argument up a slot: `Balance(, Warehouse = &W)`
			// would come back as `Balance(Warehouse = &W)` and the condition would arrive where the
			// moment belongs. Empty stays empty, and the commas count the positions.
			std::vector<ibString> args;
			args.reserve(source.m_args.size());
			for (const auto& arg : source.m_args)
				args.push_back(arg ? RenderExpr(*arg) : ibString());
			out += ibT("(") + Join(args, ibT(", ")) + ibT(")");
		}
	}

	if (!source.m_alias.IsEmpty())
		out += ibT(" ") + Kw(ibQueryKeyword::As) + ibT(" ") + source.m_alias;

	return out;
}

ibString JoinKindText(ibQueryJoinKindAst kind)
{
	switch (kind)
	{
	case ibQueryJoinKindAst::Inner: return Kw(ibQueryKeyword::Inner);
	case ibQueryJoinKindAst::Left:  return Kw(ibQueryKeyword::Left);
	case ibQueryJoinKindAst::Right: return Kw(ibQueryKeyword::Right);
	case ibQueryJoinKindAst::Full:  return Kw(ibQueryKeyword::Full);
	}
	return Kw(ibQueryKeyword::Inner);
}

// THE CLAUSE KEYWORD ALONE ON ITS LINE, ITS CONTENTS INDENTED UNDER IT. The keyword says what the
// next lines ARE, so a reader scanning the left edge sees the shape of the query — SELECT, FROM,
// WHERE, GROUP BY — and reads the details only where they matter. Everything a clause holds is a
// LIST (columns, sources, keys), and a list one item per line is the form that survives a field
// being added: one line changes, and the diff says exactly which.
//
// No alignment beyond that. The text is re-parsed and diffed far more often than it is admired,
// and clever layout is what makes a diff unreadable.
ibString RenderSelect(const ibQuerySelect& select, int indent)
{
	const ibString pad(ibT(' '), indent);
	const ibString item = pad + ibT("\t");        // where a clause's contents live
	const ibString itemSep = ibT(",\n") + item;   // one item per line
	ibString out;

	// ORDER AS THE PARSER READS IT: TOP first, then DISTINCT (queryParser.cpp,
	// ParseSelectCore — T-SQL order). Writing them the other way round produces
	// text this very language cannot read back, which is the one failure a
	// round trip must never have.
	out += pad + Kw(ibQueryKeyword::Select);
	if (select.m_allowed)  out += ibT(" ") + Kw(ibQueryKeyword::Allowed);
	if (select.m_top > 0)  out += ibString::Format(ibT(" %s %ld"), Kw(ibQueryKeyword::Top), select.m_top);
	if (select.m_distinct) out += ibT(" ") + Kw(ibQueryKeyword::Distinct);

	if (select.m_selectAll || select.m_projections.empty()) {
		out += ibT("\n") + item + ibT("*");
	}
	else {
		std::vector<ibString> cols;
		cols.reserve(select.m_projections.size());
		for (const auto& projection : select.m_projections) {
			if (projection.m_star) { cols.push_back(ibT("*")); continue; }
			ibString col = projection.m_expr ? RenderExpr(*projection.m_expr) : ibString();
			// …THE ROLE IT WAS GIVEN, where it was read: between the expression and the name. Dropped, a round
			// trip through text would hand the field back to what its source says.
			if (projection.m_roleSaid) {
				col += ibT(" ROLE ");
				col += ibBalanceRoleWord(projection.m_role);
				if (projection.m_periodRank > 0)
					col += ibString::Format(ibT(" %d"), projection.m_periodRank);
			}
			if (!projection.m_alias.IsEmpty())
				col += ibT(" ") + Kw(ibQueryKeyword::As) + ibT(" ") + projection.m_alias;
			cols.push_back(col);
		}
		out += ibT("\n") + item + Join(cols, itemSep);
	}

	// INTO comes between the projection and FROM — the clause that turns "give me these rows"
	// into "put these rows there", read in that order.
	if (!select.m_intoTemp.IsEmpty())
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Into) + ibT(" ") + select.m_intoTemp;

	// ONTO sits where INTO sits — same question, same place in the text (Max). The two never appear
	// together, so there is no order between them to decide.
	if (!select.m_ontoName.IsEmpty())
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Onto) + ibT(" ") + select.m_ontoName;

	// NO TABLE, NO `FROM`. A query being built has no source yet, and writing the keyword over
	// nothing produced `FROM` followed by emptiness — which the parser then complained about at a
	// position pointing at thin air ("expected a name"). Left out, the same parser says the true
	// thing instead: FROM is missing. An incomplete query should read as incomplete, not as broken.
	const bool hasSource = !select.m_from.m_name.empty() || select.m_from.m_subquery;
	if (hasSource)
		out += ibT("\n") + pad + Kw(ibQueryKeyword::From) + ibT("\n") + item
		     + RenderSource(select.m_from, indent + kNestedIndent);

	for (const auto& join : select.m_joins) {
		// ⭐⭐ NO CONDITION IS WRITTEN AS A COMMA — `FROM A, B`, the product. A `JOIN` with nothing
		// after it says the same thing to the machine and something else entirely to a reader: it is
		// indistinguishable from a sentence somebody stopped writing, which is why the parser now
		// refuses one.
		//
		// ⚠ AND THE TWO HALVES MUST LAND TOGETHER. I taught the parser to require ON and left this
		// writing `JOIN X` — so the constructor produced text its own parser threw back at it, on a
		// query nobody had touched. A round trip is only a round trip when both directions agree.
		if (!join.m_on) {
			out += ibT(",\n") + item + RenderSource(join.m_source, indent + kNestedIndent);
			continue;
		}
		out += ibT("\n") + pad + JoinKindText(join.m_kind) + ibT(" ") + Kw(ibQueryKeyword::Join)
			+ ibT("\n") + item + RenderSource(join.m_source, indent + kNestedIndent);
		out += ibT("\n") + item + Kw(ibQueryKeyword::On) + ibT(" ") + RenderExpr(*join.m_on);
	}

	if (select.m_where)
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Where) + ibT("\n") + item + RenderExpr(*select.m_where);

	if (!select.m_groupBy.empty()) {
		std::vector<ibString> keys;
		keys.reserve(select.m_groupBy.size());
		for (const auto& key : select.m_groupBy)
			if (key) keys.push_back(RenderExpr(*key));
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Group) + ibT(" ") + Kw(ibQueryKeyword::By)
			+ ibT("\n") + item + Join(keys, itemSep);
	}

	if (select.m_having)
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Having) + ibT("\n") + item + RenderExpr(*select.m_having);

	// UNION branches come BEFORE the trailing ORDER BY / TOTALS, which belong to
	// the whole union rather than to the last branch.
	for (const auto& branch : select.m_unions) {
		if (!branch) continue;
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Union);
		if (branch->m_unionAll) out += ibT(" ") + Kw(ibQueryKeyword::All);
		out += ibT("\n") + RenderSelect(*branch, indent);
	}

	if (!select.m_orderBy.empty()) {
		std::vector<ibString> order;
		order.reserve(select.m_orderBy.size());
		for (const auto& line : select.m_orderBy) {
			if (!line.m_expr) continue;
			ibString text = RenderExpr(*line.m_expr);
			// ASC is the default and is written anyway: an explicit direction is
			// what a reader checks, and the constructor round-trips it either way.
			text += ibT(" ") + Kw(line.m_ascending ? ibQueryKeyword::Asc : ibQueryKeyword::Desc);
			order.push_back(text);
		}
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Order) + ibT(" ") + Kw(ibQueryKeyword::By)
			+ ibT("\n") + item + Join(order, itemSep);
	}

	if (select.m_hasTotals) {
		std::vector<ibString> aggregates;
		aggregates.reserve(select.m_totalsAggregates.size());
		for (const auto& aggregate : select.m_totalsAggregates) {
			if (!aggregate.m_expr)
				continue;
			// …AND THE NAME IT WAS GIVEN. Written back exactly as a level's alias is: a rendered
			// query that dropped it would rename the column the moment the text made a round trip,
			// and the report reading `res["Qty"]` would find nothing.
			ibString written = RenderExpr(*aggregate.m_expr);
			// …AND WHAT IT IS COMPUTED OVER, written back in the same place it was read: after the
			// call, before the name. Dropped here, a query that made a round trip through text would
			// come back with every figure folded by the ladder — running, and answering a different
			// question than the one that was asked.
			// Several ticked groupings are written in BRACKETS — `OVER (Item, Warehouse)` — which is
			// what says they are ONE area rather than the first of them followed by something else.
			// A single name keeps the bare spelling, which is the common case.
			if (!aggregate.m_scope.IsEmpty())
				written += ibT(" ") + Kw(ibQueryKeyword::Over) + ibT(" ")
				         + (aggregate.m_scope.Contains(ibT(",")) ? ibT("(") + aggregate.m_scope + ibT(")")
				                                                 : aggregate.m_scope);
			if (!aggregate.m_alias.IsEmpty())
				written += ibT(" ") + Kw(ibQueryKeyword::As) + ibT(" ") + aggregate.m_alias;
			aggregates.push_back(written);
		}

		out += ibT("\n") + pad + Kw(ibQueryKeyword::Totals);
		if (!aggregates.empty())
			out += ibT("\n") + item + Join(aggregates, itemSep);

		if (!select.m_totalsBy.empty() || !select.m_totalsSplits.empty() || select.m_totalsOverall) {
			// ONE LEVEL, WRITTEN. Several fields are BRACKETED, one field is not: the bracket is the
			// only thing that tells a level of two fields from two levels when the text is read back,
			// and writing it around a lone field would change the spelling of every existing query.
			const auto levelText = [](const ibQueryTotalDim& dim) {
				std::vector<ibString> fields;
				fields.reserve(dim.m_fields.size());
				for (const ibQueryTotalField& field : dim.m_fields) {
					if (!field.m_expr) continue;
					fields.push_back(ibRenderTotalField(field));
				}
				if (fields.empty())
					return ibString();
				ibString text = fields.size() == 1
					? fields.front()
					: ibT("(") + Join(fields, ibT(", ")) + ibT(")");
				// The LEVEL's name, when it has one of its own. Written with AS, so reading it back
				// cannot be confused with the next dimension in the list.
				if (!dim.m_alias.IsEmpty())
					text += ibT(" ") + Kw(ibQueryKeyword::As) + ibT(" ") + dim.m_alias;
				return text;
			};

			// THE HIDDEN NODE FIRST — its levels are what every report has always written — and then
			// one `SPLIT` per visible node, each with its own ladder and its own name.
			//
			// 🛑 THE RENDERER IS WRITTEN THE SAME DAY THE GRAMMAR IS. `PERIODS` went into the language
			// and not into here, so the constructor's cell showed a level SHORTER than what it edited
			// and the first edit silently dropped the periodicity. A cell that shows less than it
			// edits loses the rest.
			ibString byList;
			const auto append = [&byList, &item, &itemSep](const ibString& text, bool startsNode) {
				if (byList.IsEmpty())
					byList = text;
				else if (startsNode)
					byList += ibT("\n") + item + text;   // a node is not the next item of a list
				else
					byList += itemSep + text;
			};

			// FIRST, because that is where it sits — above every dimension.
			if (select.m_totalsOverall)
				append(Kw(ibQueryKeyword::Overall), false);
			for (const ibQueryTotalDim& dim : select.m_totalsBy) {
				const ibString text = levelText(dim);
				if (!text.IsEmpty())
					append(text, false);
			}
			for (const ibQueryTotalSplit& node : select.m_totalsSplits) {
				// ⚠ A NODE WITH NOTHING ON IT WRITES NOTHING. It is a legitimate state while somebody
				// is building the query (added, not filled yet), and `SPLIT` with no level after it
				// is not a sentence this grammar has.
				std::vector<ibString> levels;
				for (const ibQueryTotalDim& dim : node.m_levels) {
					const ibString text = levelText(dim);
					if (!text.IsEmpty())
						levels.push_back(text);
				}
				if (levels.empty())
					continue;
				// `SPLIT <name> BY <levels>` — the node is named where it is OPENED, so a reader knows
				// whose block this is before reading the ladder. An unnamed node writes `SPLIT BY …`,
				// which is what says the name was left out.
				ibString text = Kw(ibQueryKeyword::Split) + ibT(" ");
				if (!node.m_name.IsEmpty())
					text += node.m_name + ibT(" ");
				text += Kw(ibQueryKeyword::By) + ibT(" ") + Join(levels, ibT(", "));
				append(text, true);
			}

			if (!byList.IsEmpty())
				out += ibT("\n") + pad + Kw(ibQueryKeyword::By) + ibT("\n") + item + byList;
		}
	}

	if (!select.m_indexBy.empty()) {
		std::vector<ibString> columns;
		columns.reserve(select.m_indexBy.size());
		for (const auto& column : select.m_indexBy)
			if (column) columns.push_back(RenderExpr(*column));
		out += ibT("\n") + pad + Kw(ibQueryKeyword::Index) + ibT(" ") + Kw(ibQueryKeyword::By)
			+ ibT("\n") + item + Join(columns, itemSep);
	}

	// LAST, and only on the outermost statement's own line: FOR UPDATE qualifies the whole read.
	if (select.m_forUpdate)
		out += ibT("\n") + pad + Kw(ibQueryKeyword::For) + ibT(" ") + Kw(ibQueryKeyword::Update);

	return out;
}

} // namespace

ibString ibRenderQuery(const ibQuerySelect& select)
{
	return RenderSelect(select, 0);
}

// ⭐⭐ ONE LINK SECTION, WRITTEN WHERE IT STANDS. `LINK Sales LEFT JOIN Plan ON … JOIN Stock ON …` —
// the head said ONCE, then a line per relation, each spelled exactly as a join inside a query is.
//
// 🛑 IT USED TO BE WRITTEN AT THE END, after every statement, and that was wrong twice over once a
// link became a statement of its own: the position it was given by the author was thrown away, and
// the statement standing in its place rendered as NOTHING — an empty `;` between two selections,
// which read back as "expected SELECT" (measured 2026-09-04, six CI failures from this one gap).
//
// A relation with no condition is not written at all: it is a row the author opened in the window
// and has not filled in, and a package saying two selections are related without saying how says
// nothing the parser could read back.
static ibString RenderLinkSection(const ibQueryPackage& package, int firstLink)
{
	if (firstLink < 0 || (std::size_t)firstLink >= package.m_links.size())
		return ibEmptyString;

	const ibString head = package.m_links[firstLink].m_left;
	if (head.IsEmpty())
		return ibEmptyString;

	ibString out = Kw(ibQueryKeyword::Link) + ibT(" ") + head;
	bool wroteOne = false;

	// THE WHOLE SECTION, which is every relation sharing this head from here on — that is what one
	// statement stands for, and what one `LINK` in the text says.
	for (std::size_t i = (std::size_t)firstLink; i < package.m_links.size(); ++i) {

		const ibQueryPackageLink& link = package.m_links[i];
		if (!link.m_left.IsSameAs(head, false))
			break;
		if (link.m_right.IsEmpty() || !link.m_on)
			continue;   // …the half-filled row, skipped where it stands

		ibString kind;
		switch (link.m_kind) {
		case ibQueryJoinKindAst::Left:  kind = Kw(ibQueryKeyword::Left)  + ibT(" "); break;
		case ibQueryJoinKindAst::Right: kind = Kw(ibQueryKeyword::Right) + ibT(" "); break;
		case ibQueryJoinKindAst::Full:  kind = Kw(ibQueryKeyword::Full)  + ibT(" "); break;
		default: break;   // INNER is the bare JOIN, as it is inside a query
		}

		out += ibT("\n\t") + kind + Kw(ibQueryKeyword::Join) + ibT(" ") + link.m_right
		     + ibT(" ") + Kw(ibQueryKeyword::On) + ibT(" ") + RenderExpr(*link.m_on);
		wroteOne = true;
	}

	// ⚠ A HEAD WITH NO RELATION WRITTEN IS NOT A SENTENCE — `LINK Sales` alone is refused on the way back.
	// Its rows are still being filled; the statement says nothing until one of them is complete.
	return wroteOne ? out : ibString();
}

ibString ibRenderQueryPackage(const ibQueryPackage& package)
{
	// ';' SEPARATES the statements of a package and does NOT terminate the last one. It is the
	// separator the parser reads back, and a package that ended with one would put a semicolon on
	// an ordinary query the moment it stopped being alone — which is punctuation the author did not
	// write and did not ask for. (A trailing ';' typed BY HAND is still accepted on the way in;
	// ParsePackage allows it. It is simply not what we produce.)
	ibString out;
	for (std::size_t i = 0; i < package.m_statements.size(); i++) {
		const ibQueryAstStatement& statement = package.m_statements[i];
		ibString text;
		if (statement.IsDrop())
			text = Kw(ibQueryKeyword::Drop) + ibT(" ") + statement.m_dropTemp;
		else if (statement.IsLink())
			text = RenderLinkSection(package, statement.m_linkIndex);
		else if (statement.m_select)
			text = RenderSelect(*statement.m_select, 0);
		// A statement with nothing to say — a link section still being filled — writes nothing at all,
		// separator included: an empty place between two `;` reads back as "expected SELECT".
		if (text.IsEmpty())
			continue;
		// THE SEPARATOR ON A LINE OF ITS OWN. A `;` hanging off the end of the last line of one
		// statement reads as part of that statement; standing alone between them it reads as what it
		// is — the boundary. The statements are many lines each, so the boundary has to be as easy
		// to find as they are.
		if (!out.IsEmpty())
			out += ibT("\n;\n");
		out += text;
	}

	return out;
}

ibString ibRenderQueryExpr(const ibQueryAstExpr& expr)
{
	return RenderExpr(expr);
}

// A primitive's qualifiers follow its name as they were written — a number as itself, a word (the date's
// composition) bare, since it was read as one: `Number(15, 2)`, `Date(Date)`.
ibString ibRenderQueryCastTarget(const ibQueryAstExpr& cast)
{
	// ⚠ Two returns, not `word ? GetString() : RenderExpr(…)`: an ibString and a wxString convert into
	// each other, so the conditional has no type of its own — clang refuses it, MSVC picks one quietly.
	const auto spell = [](const ibQueryAstExpr& q) -> ibString {
		if (q.m_kind == ibQueryAstExprKind::Literal && q.m_literal.GetType() == ibValueTypes::TYPE_STRING)
			return q.m_literal.GetString();
		return RenderExpr(q);
	};
	ibString qualifiers;
	for (const ibQueryAstExprPtr& q : cast.m_args) {
		if (!q) continue;
		qualifiers += qualifiers.IsEmpty() ? ibT("(") : ibT(", ");
		qualifiers += spell(*q);
	}
	if (!qualifiers.IsEmpty()) qualifiers += ibT(")");
	return Join(cast.m_path, ibT(".")) + qualifiers;
}

ibQueryAstExprPtr ibQueryColumnFromPath(const ibString& dottedPath)
{
	auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);

	ibString segment;
	for (size_t i = 0; i < dottedPath.length(); ++i) {
		if (dottedPath[i] == ibT('.')) { column->m_path.push_back(segment); segment.clear(); }
		else                           { segment += dottedPath[i]; }
	}
	if (!segment.IsEmpty())
		column->m_path.push_back(segment);

	return column;
}

ibString ibQueryOutputName(const ibQueryProjection& projection)
{
	if (!projection.m_alias.IsEmpty())
		return projection.m_alias;
	if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column
	    && !projection.m_expr->m_path.empty())
		return projection.m_expr->m_path.back();
	return ibEmptyString;
}

ibString ibQuerySourceName(const ibQuerySource& source)
{
	if (!source.m_alias.IsEmpty())
		return source.m_alias;
	return !source.m_name.empty() ? source.m_name.back() : ibString();
}

ibString ibQuerySourceLabel(const ibQuerySource& source)
{
	const ibString name = ibQuerySourceName(source);
	if (!name.IsEmpty())
		return name;
	return source.m_subquery ? ibString(_("(nested table)")) : ibString();
}

// ONE LEVEL FIELD, WRITTEN THE WAY THE LANGUAGE WRITES IT. Used by the query text above and by the
// constructor's cell, which is the whole reason it is a function: the two must agree, and they
// agree by being the same code rather than by looking similar.
ibString ibRenderTotalField(const ibQueryTotalField& field)
{
	if (!field.m_expr)
		return ibEmptyString;

	ibString one = ibRenderQueryExpr(*field.m_expr);
	// Elements is the default unfold — written only when it is not.
	if (field.m_unfold == ibQueryDimUnfold::Hierarchy)
		one += ibT(" ") + ibQueryKeywordText(ibQueryKeyword::Hierarchy);
	else if (field.m_unfold == ibQueryDimUnfold::HierarchyOnly)
		one += ibT(" ") + ibQueryKeywordText(ibQueryKeyword::HierarchyOnly);
	// PERIODS(unit[, from[, to]]) — written back exactly as far as it was written: a bound that was
	// left out means "from the data", and spelling one in its place would be this renderer answering
	// a question the author left open.
	if (field.m_periods) {
		one += ibT(" ") + ibQueryKeywordText(ibQueryKeyword::Periods) + ibT("(") + field.m_periods->m_unit;
		if (field.m_periods->m_from) one += ibT(", ") + ibRenderQueryExpr(*field.m_periods->m_from);
		if (field.m_periods->m_to)   one += (field.m_periods->m_from ? ibT(", ") : ibT(", , "))
		                                  + ibRenderQueryExpr(*field.m_periods->m_to);
		one += ibT(")");
	}
	return one;
}

ibString ibQueryDimensionName(const ibQueryTotalDim& dim)
{
	if (!dim.m_alias.IsEmpty())
		return dim.m_alias;
	// No alias — the level answers to its HEAD field's column name. With several fields that name
	// is the level's head and not a description of the whole key, which is exactly why a level of
	// more than one field is worth naming.
	const ibQueryTotalField* head = dim.Head();
	return head != nullptr && head->m_expr && head->m_expr->m_kind == ibQueryAstExprKind::Column
		&& !head->m_expr->m_path.empty()
		? head->m_expr->m_path.back() : ibString();
}
