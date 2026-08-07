////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — AST back to query text (the return trip)
////////////////////////////////////////////////////////////////////////////

#include "queryRender.h"

#include "queryKeywords.h"

#include "backend/compiler/value.h"

namespace {

// Every keyword comes from the ACTIVE table, never from a literal here — that is
// what keeps a localized table rendering in the language it parses.
wxString Kw(ibQueryKeyword kw)
{
	return ibQueryKeywordText(kw);
}

wxString RenderExpr(const ibQueryAstExpr& expr);
wxString RenderSelect(const ibQuerySelect& select, int indent);

wxString Join(const std::vector<wxString>& parts, const wxString& separator)
{
	wxString out;
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
wxString RenderLiteral(const ibValue& value)
{
	switch (value.GetType())
	{
	case ibValueTypes::TYPE_STRING:
	{
		wxString text = value.GetString();
		text.Replace(wxT("\""), wxT("\"\""));
		return wxT("\"") + text + wxT("\"");
	}
	case ibValueTypes::TYPE_BOOLEAN:
		return value.GetBoolean() ? Kw(ibQueryKeyword::True) : Kw(ibQueryKeyword::False);
	case ibValueTypes::TYPE_NULL:
		return Kw(ibQueryKeyword::Null);
	case ibValueTypes::TYPE_DATE:
	{
		const wxDateTime date = value.GetDateTime();
		if (!date.IsValid())
			return Kw(ibQueryKeyword::Null);
		// DATETIME(y, m, d[, h, mi, s]) — the form the lexer parses. The time half
		// is written only when it carries something, so a plain date stays plain.
		if (date.GetHour() == 0 && date.GetMinute() == 0 && date.GetSecond() == 0) {
			return wxString::Format(wxT("DATETIME(%d, %d, %d)"),
				date.GetYear(), date.GetMonth() + 1, date.GetDay());
		}
		return wxString::Format(wxT("DATETIME(%d, %d, %d, %d, %d, %d)"),
			date.GetYear(), date.GetMonth() + 1, date.GetDay(),
			date.GetHour(), date.GetMinute(), date.GetSecond());
	}
	case ibValueTypes::TYPE_EMPTY:
		return Kw(ibQueryKeyword::Null);
	default:
		return value.GetString();
	}
}

wxString CompareOpText(ibQueryCompareOp op)
{
	switch (op)
	{
	case ibQueryCompareOp::Eq: return wxT("=");
	case ibQueryCompareOp::Ne: return wxT("<>");
	case ibQueryCompareOp::Lt: return wxT("<");
	case ibQueryCompareOp::Le: return wxT("<=");
	case ibQueryCompareOp::Gt: return wxT(">");
	case ibQueryCompareOp::Ge: return wxT(">=");
	}
	return wxT("=");
}

wxString ArithOpText(ibQueryArithOp op)
{
	switch (op)
	{
	case ibQueryArithOp::Add: return wxT("+");
	case ibQueryArithOp::Sub: return wxT("-");
	case ibQueryArithOp::Mul: return wxT("*");
	case ibQueryArithOp::Div: return wxT("/");
	case ibQueryArithOp::Mod: return wxT("%");
	}
	return wxT("+");
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
	case ibQueryAstExprKind::Arith:
	case ibQueryAstExprKind::Not:
		return true;
	default:
		return false;
	}
}

wxString RenderChild(const ibQueryAstExprPtr& child)
{
	if (!child)
		return wxEmptyString;
	const wxString text = RenderExpr(*child);
	return NeedsParens(*child) ? wxT("(") + text + wxT(")") : text;
}

wxString RenderExpr(const ibQueryAstExpr& expr)
{
	switch (expr.m_kind)
	{
	case ibQueryAstExprKind::Column:
		// A WALK ROOTED ON A CAST writes its root first: `CAST(x AS T).A.B`. The root is in m_arg for
		// exactly this shape and for nothing else, so a plain column is unaffected.
		if (expr.m_arg && expr.m_arg->m_kind == ibQueryAstExprKind::Cast)
			return RenderExpr(*expr.m_arg) + wxT(".") + Join(expr.m_path, wxT("."));
		return Join(expr.m_path, wxT("."));

	case ibQueryAstExprKind::Literal:
		return RenderLiteral(expr.m_literal);

	case ibQueryAstExprKind::Param:
		return wxT("&") + expr.m_paramName;

	case ibQueryAstExprKind::Value:
		return Kw(ibQueryKeyword::Value) + wxT("(") + Join(expr.m_path, wxT(".")) + wxT(")");

	case ibQueryAstExprKind::Cast:
		return Kw(ibQueryKeyword::Cast) + wxT("(") + (expr.m_arg ? RenderExpr(*expr.m_arg) : wxString())
			+ wxT(" ") + Kw(ibQueryKeyword::As) + wxT(" ") + Join(expr.m_path, wxT(".")) + wxT(")");

	case ibQueryAstExprKind::Func:
	{
		const wxString arg = expr.m_star ? wxString(wxT("*")) : RenderExpr(*expr.m_arg);
		const wxString distinct = expr.m_distinctArg ? Kw(ibQueryKeyword::Distinct) + wxT(" ") : wxString();
		return Kw(expr.m_func) + wxT("(") + distinct + arg + wxT(")");
	}

	case ibQueryAstExprKind::Arith:
		return RenderChild(expr.m_lhs) + wxT(" ") + ArithOpText(expr.m_arith)
			+ wxT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Compare:
		return RenderChild(expr.m_lhs) + wxT(" ") + CompareOpText(expr.m_cmp)
			+ wxT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Like:
		return RenderChild(expr.m_lhs)
			+ (expr.m_negated ? wxT(" ") + Kw(ibQueryKeyword::Not) : wxString())
			+ wxT(" ") + Kw(ibQueryKeyword::Like) + wxT(" ") + RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::In:
	{
		wxString out = RenderChild(expr.m_lhs);
		if (expr.m_negated) out += wxT(" ") + Kw(ibQueryKeyword::Not);
		out += wxT(" ") + Kw(ibQueryKeyword::In) + wxT(" (");
		if (expr.m_subquery) {
			out += RenderSelect(*expr.m_subquery, 0);
		}
		else {
			std::vector<wxString> items;
			items.reserve(expr.m_list.size());
			for (const auto& item : expr.m_list)
				if (item) items.push_back(RenderExpr(*item));
			out += Join(items, wxT(", "));
		}
		return out + wxT(")");
	}

	case ibQueryAstExprKind::IsNull:
		return RenderChild(expr.m_lhs) + wxT(" ") + Kw(ibQueryKeyword::Is)
			+ (expr.m_negated ? wxT(" ") + Kw(ibQueryKeyword::Not) : wxString())
			+ wxT(" ") + Kw(ibQueryKeyword::Null);

	case ibQueryAstExprKind::Between:
		return RenderChild(expr.m_lhs)
			+ (expr.m_negated ? wxT(" ") + Kw(ibQueryKeyword::Not) : wxString())
			+ wxT(" ") + Kw(ibQueryKeyword::Between) + wxT(" ") + RenderChild(expr.m_low)
			+ wxT(" ") + Kw(ibQueryKeyword::And) + wxT(" ") + RenderChild(expr.m_high);

	case ibQueryAstExprKind::Logical:
		return RenderChild(expr.m_lhs)
			+ wxT(" ") + Kw(expr.m_isOr ? ibQueryKeyword::Or : ibQueryKeyword::And) + wxT(" ")
			+ RenderChild(expr.m_rhs);

	case ibQueryAstExprKind::Not:
		return Kw(ibQueryKeyword::Not) + wxT(" ") + RenderChild(expr.m_lhs);

	case ibQueryAstExprKind::Case:
	{
		wxString out = Kw(ibQueryKeyword::Case);
		for (const auto& branch : expr.m_cases) {
			out += wxT(" ") + Kw(ibQueryKeyword::When) + wxT(" ") + RenderExpr(*branch.first);
			out += wxT(" ") + Kw(ibQueryKeyword::Then) + wxT(" ") + RenderExpr(*branch.second);
		}
		if (expr.m_else)
			out += wxT(" ") + Kw(ibQueryKeyword::Else) + wxT(" ") + RenderExpr(*expr.m_else);
		return out + wxT(" ") + Kw(ibQueryKeyword::End);
	}
	}

	return wxEmptyString;
}

wxString RenderSource(const ibQuerySource& source)
{
	wxString out;
	if (source.m_subquery) {
		out = wxT("(") + RenderSelect(*source.m_subquery, 0) + wxT(")");
	}
	else {
		// The `&` goes back on: what it writes, the parser reads — and a table handed in from
		// outside has to still say so after a round trip.
		out = (source.m_parameter ? wxT("&") : wxT("")) + Join(source.m_name, wxT("."));
		if (!source.m_args.empty()) {
			std::vector<wxString> args;
			args.reserve(source.m_args.size());
			for (const auto& arg : source.m_args)
				if (arg) args.push_back(RenderExpr(*arg));
			out += wxT("(") + Join(args, wxT(", ")) + wxT(")");
		}
	}

	if (!source.m_alias.IsEmpty())
		out += wxT(" ") + Kw(ibQueryKeyword::As) + wxT(" ") + source.m_alias;

	return out;
}

wxString JoinKindText(ibQueryJoinKindAst kind)
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
wxString RenderSelect(const ibQuerySelect& select, int indent)
{
	const wxString pad(wxT(' '), indent);
	const wxString item = pad + wxT("\t");        // where a clause's contents live
	const wxString itemSep = wxT(",\n") + item;   // one item per line
	wxString out;

	// ORDER AS THE PARSER READS IT: TOP first, then DISTINCT (queryParser.cpp,
	// ParseSelectCore — T-SQL order). Writing them the other way round produces
	// text this very language cannot read back, which is the one failure a
	// round trip must never have.
	out += pad + Kw(ibQueryKeyword::Select);
	if (select.m_allowed)  out += wxT(" ") + Kw(ibQueryKeyword::Allowed);
	if (select.m_top > 0)  out += wxString::Format(wxT(" %s %ld"), Kw(ibQueryKeyword::Top), select.m_top);
	if (select.m_distinct) out += wxT(" ") + Kw(ibQueryKeyword::Distinct);

	if (select.m_selectAll || select.m_projections.empty()) {
		out += wxT("\n") + item + wxT("*");
	}
	else {
		std::vector<wxString> cols;
		cols.reserve(select.m_projections.size());
		for (const auto& projection : select.m_projections) {
			if (projection.m_star) { cols.push_back(wxT("*")); continue; }
			wxString col = projection.m_expr ? RenderExpr(*projection.m_expr) : wxString();
			if (!projection.m_alias.IsEmpty())
				col += wxT(" ") + Kw(ibQueryKeyword::As) + wxT(" ") + projection.m_alias;
			cols.push_back(col);
		}
		out += wxT("\n") + item + Join(cols, itemSep);
	}

	// INTO comes between the projection and FROM — the clause that turns "give me these rows"
	// into "put these rows there", read in that order.
	if (!select.m_intoTemp.IsEmpty())
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Into) + wxT(" ") + select.m_intoTemp;

	// NO TABLE, NO `FROM`. A query being built has no source yet, and writing the keyword over
	// nothing produced `FROM` followed by emptiness — which the parser then complained about at a
	// position pointing at thin air ("expected a name"). Left out, the same parser says the true
	// thing instead: FROM is missing. An incomplete query should read as incomplete, not as broken.
	const bool hasSource = !select.m_from.m_name.empty() || select.m_from.m_subquery;
	if (hasSource)
		out += wxT("\n") + pad + Kw(ibQueryKeyword::From) + wxT("\n") + item + RenderSource(select.m_from);

	for (const auto& join : select.m_joins) {
		out += wxT("\n") + pad + JoinKindText(join.m_kind) + wxT(" ") + Kw(ibQueryKeyword::Join)
			+ wxT("\n") + item + RenderSource(join.m_source);
		// An omitted ON is not the same as `ON TRUE`: it means "join by reference",
		// and writing one in would turn an auto-join into a cross join.
		if (join.m_on)
			out += wxT("\n") + item + Kw(ibQueryKeyword::On) + wxT(" ") + RenderExpr(*join.m_on);
	}

	if (select.m_where)
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Where) + wxT("\n") + item + RenderExpr(*select.m_where);

	if (!select.m_groupBy.empty()) {
		std::vector<wxString> keys;
		keys.reserve(select.m_groupBy.size());
		for (const auto& key : select.m_groupBy)
			if (key) keys.push_back(RenderExpr(*key));
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Group) + wxT(" ") + Kw(ibQueryKeyword::By)
			+ wxT("\n") + item + Join(keys, itemSep);
	}

	if (select.m_having)
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Having) + wxT("\n") + item + RenderExpr(*select.m_having);

	// UNION branches come BEFORE the trailing ORDER BY / TOTALS, which belong to
	// the whole union rather than to the last branch.
	for (const auto& branch : select.m_unions) {
		if (!branch) continue;
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Union);
		if (branch->m_unionAll) out += wxT(" ") + Kw(ibQueryKeyword::All);
		out += wxT("\n") + RenderSelect(*branch, indent);
	}

	if (!select.m_orderBy.empty()) {
		std::vector<wxString> order;
		order.reserve(select.m_orderBy.size());
		for (const auto& line : select.m_orderBy) {
			if (!line.m_expr) continue;
			wxString text = RenderExpr(*line.m_expr);
			// ASC is the default and is written anyway: an explicit direction is
			// what a reader checks, and the constructor round-trips it either way.
			text += wxT(" ") + Kw(line.m_ascending ? ibQueryKeyword::Asc : ibQueryKeyword::Desc);
			order.push_back(text);
		}
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Order) + wxT(" ") + Kw(ibQueryKeyword::By)
			+ wxT("\n") + item + Join(order, itemSep);
	}

	if (select.m_hasTotals) {
		std::vector<wxString> aggregates;
		aggregates.reserve(select.m_totalsAggregates.size());
		for (const auto& aggregate : select.m_totalsAggregates)
			if (aggregate) aggregates.push_back(RenderExpr(*aggregate));

		out += wxT("\n") + pad + Kw(ibQueryKeyword::Totals);
		if (!aggregates.empty())
			out += wxT("\n") + item + Join(aggregates, itemSep);

		if (!select.m_totalsBy.empty() || select.m_totalsOverall) {
			std::vector<wxString> dims;
			dims.reserve(select.m_totalsBy.size() + 1);
			// FIRST, because that is where it sits — above every dimension.
			if (select.m_totalsOverall)
				dims.push_back(Kw(ibQueryKeyword::Overall));
			for (const auto& dim : select.m_totalsBy) {
				if (!dim.m_expr) continue;
				wxString text = RenderExpr(*dim.m_expr);
				// Elements is the default unfold — written only when it is not.
				if (dim.m_unfold == ibQueryDimUnfold::Hierarchy)
					text += wxT(" ") + Kw(ibQueryKeyword::Hierarchy);
				else if (dim.m_unfold == ibQueryDimUnfold::HierarchyOnly)
					text += wxT(" ") + Kw(ibQueryKeyword::HierarchyOnly);
				// The LEVEL's name, when it has one of its own. Written with AS, so reading it back
				// cannot be confused with the next dimension in the list.
				if (!dim.m_alias.IsEmpty())
					text += wxT(" ") + Kw(ibQueryKeyword::As) + wxT(" ") + dim.m_alias;
				dims.push_back(text);
			}
			out += wxT("\n") + pad + Kw(ibQueryKeyword::By) + wxT("\n") + item + Join(dims, itemSep);
		}
	}

	if (!select.m_indexBy.empty()) {
		std::vector<wxString> columns;
		columns.reserve(select.m_indexBy.size());
		for (const auto& column : select.m_indexBy)
			if (column) columns.push_back(RenderExpr(*column));
		out += wxT("\n") + pad + Kw(ibQueryKeyword::Index) + wxT(" ") + Kw(ibQueryKeyword::By)
			+ wxT("\n") + item + Join(columns, itemSep);
	}

	// LAST, and only on the outermost statement's own line: FOR UPDATE qualifies the whole read.
	if (select.m_forUpdate)
		out += wxT("\n") + pad + Kw(ibQueryKeyword::For) + wxT(" ") + Kw(ibQueryKeyword::Update);

	return out;
}

} // namespace

wxString ibRenderQuery(const ibQuerySelect& select)
{
	return RenderSelect(select, 0);
}

wxString ibRenderQueryPackage(const ibQueryPackage& package)
{
	// ';' SEPARATES the statements of a package and does NOT terminate the last one. It is the
	// separator the parser reads back, and a package that ended with one would put a semicolon on
	// an ordinary query the moment it stopped being alone — which is punctuation the author did not
	// write and did not ask for. (A trailing ';' typed BY HAND is still accepted on the way in;
	// ParsePackage allows it. It is simply not what we produce.)
	wxString out;
	for (std::size_t i = 0; i < package.m_statements.size(); i++) {
		const ibQueryAstStatement& statement = package.m_statements[i];
		// THE SEPARATOR ON A LINE OF ITS OWN. A `;` hanging off the end of the last line of one
		// statement reads as part of that statement; standing alone between them it reads as what it
		// is — the boundary. The statements are many lines each, so the boundary has to be as easy
		// to find as they are.
		if (i > 0)
			out += wxT("\n;\n");
		if (statement.IsDrop())
			out += Kw(ibQueryKeyword::Drop) + wxT(" ") + statement.m_dropTemp;
		else if (statement.m_select)
			out += RenderSelect(*statement.m_select, 0);
	}
	return out;
}

wxString ibRenderQueryExpr(const ibQueryAstExpr& expr)
{
	return RenderExpr(expr);
}

ibQueryAstExprPtr ibQueryColumnFromPath(const wxString& dottedPath)
{
	auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);

	wxString segment;
	for (size_t i = 0; i < dottedPath.length(); ++i) {
		if (dottedPath[i] == wxT('.')) { column->m_path.push_back(segment); segment.clear(); }
		else                           { segment += dottedPath[i]; }
	}
	if (!segment.IsEmpty())
		column->m_path.push_back(segment);

	return column;
}

wxString ibQueryOutputName(const ibQueryProjection& projection)
{
	if (!projection.m_alias.IsEmpty())
		return projection.m_alias;
	if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column
	    && !projection.m_expr->m_path.empty())
		return projection.m_expr->m_path.back();
	return wxEmptyString;
}

wxString ibQuerySourceName(const ibQuerySource& source)
{
	if (!source.m_alias.IsEmpty())
		return source.m_alias;
	return !source.m_name.empty() ? source.m_name.back() : wxString();
}

wxString ibQuerySourceLabel(const ibQuerySource& source)
{
	const wxString name = ibQuerySourceName(source);
	if (!name.IsEmpty())
		return name;
	return source.m_subquery ? wxString(_("(nested table)")) : wxString();
}

wxString ibQueryDimensionName(const ibQueryTotalDim& dim)
{
	if (!dim.m_alias.IsEmpty())
		return dim.m_alias;
	return dim.m_expr && dim.m_expr->m_kind == ibQueryAstExprKind::Column && !dim.m_expr->m_path.empty()
		? dim.m_expr->m_path.back() : wxString();
}
