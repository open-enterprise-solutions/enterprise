////////////////////////////////////////////////////////////////////////////
//	L4-1 — text query language parser (queryParser.h)
////////////////////////////////////////////////////////////////////////////

#include "queryParser.h"

#include "backend/backend_exception.h"   // ibBackendCoreException

namespace {

bool IsAggregateKw(const ibQueryToken& t)
{
	if (t.m_kind != ibQueryTokenKind::Keyword) return false;
	switch (t.m_keyword) {
	case ibQueryKeyword::Sum:
	case ibQueryKeyword::Count:
	case ibQueryKeyword::Min:
	case ibQueryKeyword::Max:
	case ibQueryKeyword::Avg:
		return true;
	default:
		return false;
	}
}

} // namespace

//////////////////////////////////////////////////////////////////////
// token cursor helpers
//////////////////////////////////////////////////////////////////////

bool ibQueryParser::AcceptKw(ibQueryKeyword kw)
{
	if (Cur().IsKeyword(kw)) { ++m_pos; return true; }
	return false;
}

void ibQueryParser::ExpectKw(ibQueryKeyword kw, const wxChar* what)
{
	if (!AcceptKw(kw))
		Fail(Cur(), wxString::Format(_("expected %s"), what));
}

bool ibQueryParser::AcceptPunct(wxChar c)
{
	if (Cur().IsPunct(c)) { ++m_pos; return true; }
	return false;
}

void ibQueryParser::ExpectPunct(wxChar c, const wxChar* what)
{
	if (!AcceptPunct(c))
		Fail(Cur(), wxString::Format(_("expected %s"), what));
}

bool ibQueryParser::AcceptOp(const wxChar* op)
{
	if (Cur().IsOp(op)) { ++m_pos; return true; }
	return false;
}

void ibQueryParser::Fail(const ibQueryToken& at, const wxString& msg) const
{
	ibBackendCoreException::Error(_("Query syntax error at line %u (position %u): %s"),
		at.m_line, at.m_col, msg);
}

//////////////////////////////////////////////////////////////////////
// entry
//////////////////////////////////////////////////////////////////////

ibQuerySelectPtr ibQueryParser::Parse(const wxString& queryText)
{
	ibQueryLexer lexer;
	m_toks = lexer.Tokenize(queryText);
	m_pos = 0;

	ibQuerySelectPtr sel = ParseSelectStatement();

	if (!Cur().IsEnd())
		Fail(Cur(), _("unexpected text after the query"));

	return sel;
}

// One SELECT BODY up to HAVING (no ORDER / TOTALS) — a single branch of a (possible) UNION.
ibQuerySelectPtr ibQueryParser::ParseSelectCore()
{
	auto sel = std::make_shared<ibQuerySelect>();

	ExpectKw(ibQueryKeyword::Select, wxT("SELECT"));

	// SELECT TOP n — a positive integer literal row limit (before DISTINCT, T-SQL order).
	if (AcceptKw(ibQueryKeyword::Top)) {
		const ibQueryToken& n = Cur();
		if (n.m_kind != ibQueryTokenKind::Number)
			Fail(n, _("expected a number after TOP"));
		sel->m_top = n.m_literal.GetInteger();
		if (sel->m_top <= 0)
			Fail(n, _("TOP expects a positive row count"));
		++m_pos;
	}

	if (AcceptKw(ibQueryKeyword::Distinct)) sel->m_distinct = true;
	ParseSelectList(*sel);

	ExpectKw(ibQueryKeyword::From, wxT("FROM"));
	sel->m_from = ParseSource();
	ParseJoins(*sel);

	if (AcceptKw(ibQueryKeyword::Where))
		sel->m_where = ParsePredicate();

	if (AcceptKw(ibQueryKeyword::Group)) {
		ExpectKw(ibQueryKeyword::By, wxT("BY"));
		do { sel->m_groupBy.push_back(ParsePrimary()); } while (AcceptPunct(wxT(',')));
		if (AcceptKw(ibQueryKeyword::Having))
			sel->m_having = ParsePredicate();
	}
	return sel;
}

// A full SELECT statement: the first branch, any UNION [ALL] branches (stacked vertically), then the
// trailing ORDER BY / TOTALS that apply to the WHOLE result. Re-entered for a subquery `( SELECT … )`.
ibQuerySelectPtr ibQueryParser::ParseSelectStatement()
{
	ibQuerySelectPtr sel = ParseSelectCore();

	while (AcceptKw(ibQueryKeyword::Union)) {
		const bool all = AcceptKw(ibQueryKeyword::All);   // UNION ALL keeps duplicates; plain UNION dedupes
		ibQuerySelectPtr branch = ParseSelectCore();
		branch->m_unionAll = all;
		sel->m_unions.push_back(branch);
	}

	if (AcceptKw(ibQueryKeyword::Order)) {
		ExpectKw(ibQueryKeyword::By, wxT("BY"));
		ParseOrderBy(*sel);
	}

	if (AcceptKw(ibQueryKeyword::Totals))
		ParseTotals(*sel);

	return sel;
}

//////////////////////////////////////////////////////////////////////
// SELECT list
//////////////////////////////////////////////////////////////////////

void ibQueryParser::ParseSelectList(ibQuerySelect& sel)
{
	if (Cur().IsOp(wxT("*"))) {
		++m_pos;
		sel.m_selectAll = true;
		return;
	}
	do {
		sel.m_projections.push_back(ParseProjection());
	} while (AcceptPunct(wxT(',')));
}

ibQueryProjection ibQueryParser::ParseProjection()
{
	ibQueryProjection p;
	p.m_expr = IsAggregateKw(Cur()) ? ParseAggregate() : ParseAddSub();   // column path, arithmetic, or CASE

	// AS alias, or an implicit bare-identifier alias (SELECT Code c)
	if (AcceptKw(ibQueryKeyword::As)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			Fail(Cur(), _("expected an alias name after AS"));
		p.m_alias = Next().m_text;
	}
	else if (Cur().m_kind == ibQueryTokenKind::Ident) {
		p.m_alias = Next().m_text;
	}
	return p;
}

//////////////////////////////////////////////////////////////////////
// FROM / JOIN sources
//////////////////////////////////////////////////////////////////////

ibQuerySource ibQueryParser::ParseSource()
{
	ibQuerySource s;

	// subquery source: FROM ( SELECT … ) [AS] alias
	if (Cur().IsPunct(wxT('(')) && Peek().IsKeyword(ibQueryKeyword::Select)) {
		++m_pos;                                   // '('
		s.m_subquery = ParseSelectStatement();
		ExpectPunct(wxT(')'), wxT("')' after a subquery"));
	}
	else {
		s.m_name = ParseDottedName();

		// optional source-call args — Balance(&Period, &Filter): value exprs handed to the
		// queryable's CreateQueryable (e.g. a register virtual table's as-of period / filter).
		if (AcceptPunct(wxT('('))) {
			if (!Cur().IsPunct(wxT(')'))) {
				do { s.m_args.push_back(ParsePrimary()); } while (AcceptPunct(wxT(',')));
			}
			ExpectPunct(wxT(')'), wxT("')' after the source arguments"));
		}
	}

	if (AcceptKw(ibQueryKeyword::As)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			Fail(Cur(), _("expected an alias name after AS"));
		s.m_alias = Next().m_text;
	}
	else if (Cur().m_kind == ibQueryTokenKind::Ident) {
		s.m_alias = Next().m_text;
	}
	return s;
}

void ibQueryParser::ParseJoins(ibQuerySelect& sel)
{
	for (;;) {
		ibQueryJoinKindAst kind;
		if      (AcceptKw(ibQueryKeyword::Inner)) {                                   ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Inner; }
		else if (AcceptKw(ibQueryKeyword::Left))  { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Left;  }
		else if (AcceptKw(ibQueryKeyword::Right)) { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Right; }
		else if (AcceptKw(ibQueryKeyword::Full))  { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Full;  }
		else if (AcceptKw(ibQueryKeyword::Join))  {                                                                               kind = ibQueryJoinKindAst::Inner; }
		else break;

		ibQueryAstJoin j;
		j.m_kind = kind;
		j.m_source = ParseSource();
		if (AcceptKw(ibQueryKeyword::On))
			j.m_on = ParsePredicate();
		sel.m_joins.push_back(std::move(j));
	}
}

void ibQueryParser::ParseOrderBy(ibQuerySelect& sel)
{
	do {
		ibQueryOrderItem it;
		it.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		it.m_expr->m_path = ParseDottedName();
		if (AcceptKw(ibQueryKeyword::Desc))      it.m_ascending = false;
		else if (AcceptKw(ibQueryKeyword::Asc))  it.m_ascending = true;
		sel.m_orderBy.push_back(std::move(it));
	} while (AcceptPunct(wxT(',')));
}

void ibQueryParser::ParseTotals(ibQuerySelect& sel)
{
	// TOTALS [aggregate {',' aggregate}] BY totalDim {',' totalDim}
	// The aggregate list is OPTIONAL — "TOTALS BY dim" is a pure grouping / hierarchy
	// with no rolled aggregate (a dynamic list grouped by a field). If BY follows
	// TOTALS immediately, there are no aggregates.
	sel.m_hasTotals = true;

	if (!AcceptKw(ibQueryKeyword::By)) {
		do {
			if (!IsAggregateKw(Cur()))
				Fail(Cur(), _("expected an aggregate function (SUM/COUNT/MIN/MAX/AVG) or BY in TOTALS"));
			sel.m_totalsAggregates.push_back(ParseAggregate());
		} while (AcceptPunct(wxT(',')));

		ExpectKw(ibQueryKeyword::By, wxT("BY in TOTALS"));
	}

	do {
		ibQueryTotalDim d;
		d.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		d.m_expr->m_path = ParseDottedName();
		if (AcceptKw(ibQueryKeyword::HierarchyOnly))     d.m_unfold = ibQueryDimUnfold::HierarchyOnly;
		else if (AcceptKw(ibQueryKeyword::Hierarchy))    d.m_unfold = ibQueryDimUnfold::Hierarchy;
		else if (AcceptKw(ibQueryKeyword::Elements))     d.m_unfold = ibQueryDimUnfold::Elements;
		sel.m_totalsBy.push_back(std::move(d));
	} while (AcceptPunct(wxT(',')));
}

std::vector<wxString> ibQueryParser::ParseDottedName()
{
	std::vector<wxString> parts;
	if (Cur().m_kind != ibQueryTokenKind::Ident)
		Fail(Cur(), _("expected a name"));
	parts.push_back(Next().m_text);
	while (AcceptPunct(wxT('.'))) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			Fail(Cur(), _("expected a name after '.'"));
		parts.push_back(Next().m_text);
	}
	return parts;
}

//////////////////////////////////////////////////////////////////////
// predicate — OR < AND < NOT < comparison < primary
//////////////////////////////////////////////////////////////////////

ibQueryAstExprPtr ibQueryParser::ParsePredicate()
{
	ibQueryAstExprPtr lhs = ParseAnd();
	while (AcceptKw(ibQueryKeyword::Or)) {
		ibQueryAstExprPtr rhs = ParseAnd();
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		n->m_isOr = true; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

ibQueryAstExprPtr ibQueryParser::ParseAnd()
{
	ibQueryAstExprPtr lhs = ParseNot();
	while (AcceptKw(ibQueryKeyword::And)) {
		ibQueryAstExprPtr rhs = ParseNot();
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		n->m_isOr = false; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

ibQueryAstExprPtr ibQueryParser::ParseNot()
{
	if (AcceptKw(ibQueryKeyword::Not)) {
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
		n->m_lhs = ParseNot();
		return n;
	}
	return ParseComparison();
}

ibQueryAstExprPtr ibQueryParser::ParseComparison()
{
	ibQueryAstExprPtr lhs = ParseAddSub();

	// binary comparison operator
	if (Cur().m_kind == ibQueryTokenKind::Op) {
		const wxString& o = Cur().m_text;
		ibQueryCompareOp op;
		if      (o == wxT("="))  op = ibQueryCompareOp::Eq;
		else if (o == wxT("<>")) op = ibQueryCompareOp::Ne;
		else if (o == wxT("<"))  op = ibQueryCompareOp::Lt;
		else if (o == wxT("<=")) op = ibQueryCompareOp::Le;
		else if (o == wxT(">"))  op = ibQueryCompareOp::Gt;
		else if (o == wxT(">=")) op = ibQueryCompareOp::Ge;
		else return lhs;   // a leftover arithmetic op cannot appear here (ParseAddSub consumed them)
		++m_pos;
		ibQueryAstExprPtr rhs = ParseAddSub();
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Compare);
		n->m_cmp = op; n->m_lhs = lhs; n->m_rhs = rhs;
		return n;
	}

	// [NOT] LIKE / IN / BETWEEN
	const bool negated = AcceptKw(ibQueryKeyword::Not);

	if (AcceptKw(ibQueryKeyword::Like)) {
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Like);
		n->m_negated = negated; n->m_lhs = lhs; n->m_rhs = ParseAddSub();
		return n;
	}
	if (AcceptKw(ibQueryKeyword::In)) {
		ExpectPunct(wxT('('), wxT("'(' after IN"));
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::In);
		n->m_negated = negated; n->m_lhs = lhs;
		if (Cur().IsKeyword(ibQueryKeyword::Select))          // lhs IN (SELECT …) — subquery form
			n->m_subquery = ParseSelectStatement();
		else
			do { n->m_list.push_back(ParseAddSub()); } while (AcceptPunct(wxT(',')));
		ExpectPunct(wxT(')'), wxT("')'"));
		return n;
	}
	if (AcceptKw(ibQueryKeyword::Between)) {
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Between);
		n->m_negated = negated; n->m_lhs = lhs;
		n->m_low = ParseAddSub();
		ExpectKw(ibQueryKeyword::And, wxT("AND in BETWEEN"));
		n->m_high = ParseAddSub();
		return n;
	}

	// IS [NOT] NULL  (negation comes AFTER IS, so it cannot have been consumed above)
	if (!negated && AcceptKw(ibQueryKeyword::Is)) {
		const bool isNeg = AcceptKw(ibQueryKeyword::Not);
		ExpectKw(ibQueryKeyword::Null, wxT("NULL after IS"));
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::IsNull);
		n->m_negated = isNeg; n->m_lhs = lhs;
		return n;
	}

	if (negated)
		Fail(Cur(), _("expected LIKE / IN / BETWEEN after NOT"));

	// bare primary (a boolean column / a parenthesized predicate)
	return lhs;
}

// Arithmetic — standard precedence (+ - below * / %). The column-based L3 door does not execute a
// computed expression yet, so the lowering rejects an Arith node; the parser stays complete.
ibQueryAstExprPtr ibQueryParser::ParseAddSub()
{
	ibQueryAstExprPtr lhs = ParseMulDiv();
	for (;;) {
		ibQueryArithOp op;
		if      (Cur().IsOp(wxT("+"))) op = ibQueryArithOp::Add;
		else if (Cur().IsOp(wxT("-"))) op = ibQueryArithOp::Sub;
		else break;
		const ibQueryToken at = Cur(); ++m_pos;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		n->m_arith = op; n->m_lhs = lhs; n->m_rhs = ParseMulDiv();
		n->m_line = at.m_line; n->m_col = at.m_col;
		lhs = n;
	}
	return lhs;
}

ibQueryAstExprPtr ibQueryParser::ParseMulDiv()
{
	ibQueryAstExprPtr lhs = ParsePrimary();
	for (;;) {
		ibQueryArithOp op;
		if      (Cur().IsOp(wxT("*"))) op = ibQueryArithOp::Mul;
		else if (Cur().IsOp(wxT("/"))) op = ibQueryArithOp::Div;
		else if (Cur().IsOp(wxT("%"))) op = ibQueryArithOp::Mod;
		else break;
		const ibQueryToken at = Cur(); ++m_pos;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		n->m_arith = op; n->m_lhs = lhs; n->m_rhs = ParsePrimary();
		n->m_line = at.m_line; n->m_col = at.m_col;
		lhs = n;
	}
	return lhs;
}

// CASE WHEN <predicate> THEN <expr> [WHEN … THEN …] [ELSE <expr>] END (searched form).
ibQueryAstExprPtr ibQueryParser::ParseCase()
{
	const ibQueryToken kw = Cur();
	ExpectKw(ibQueryKeyword::Case, wxT("CASE"));
	auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Case);
	n->m_line = kw.m_line; n->m_col = kw.m_col;

	do {
		ExpectKw(ibQueryKeyword::When, wxT("WHEN"));
		ibQueryAstExprPtr cond = ParsePredicate();
		ExpectKw(ibQueryKeyword::Then, wxT("THEN"));
		ibQueryAstExprPtr val = ParseAddSub();
		n->m_cases.emplace_back(cond, val);
	} while (Cur().IsKeyword(ibQueryKeyword::When));

	if (AcceptKw(ibQueryKeyword::Else))
		n->m_else = ParseAddSub();
	ExpectKw(ibQueryKeyword::End, wxT("END"));
	return n;
}

ibQueryAstExprPtr ibQueryParser::ParsePrimary()
{
	const ibQueryToken& tk = Cur();

	// parenthesized sub-predicate / arithmetic
	if (tk.IsPunct(wxT('('))) {
		++m_pos;
		ibQueryAstExprPtr e = ParsePredicate();
		ExpectPunct(wxT(')'), wxT("')'"));
		return e;
	}

	// CASE expression
	if (tk.IsKeyword(ibQueryKeyword::Case))
		return ParseCase();

	// aggregate function
	if (IsAggregateKw(tk))
		return ParseAggregate();

	// keyword literals: TRUE / FALSE / NULL
	if (tk.IsKeyword(ibQueryKeyword::True)) {
		++m_pos;
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		e->m_literal.SetBoolean(ibQueryKeywordText(ibQueryKeyword::True));
		return e;
	}
	if (tk.IsKeyword(ibQueryKeyword::False)) {
		++m_pos;
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		e->m_literal.SetBoolean(ibQueryKeywordText(ibQueryKeyword::False));
		return e;
	}
	if (tk.IsKeyword(ibQueryKeyword::Null)) {
		++m_pos;
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		e->m_literal.SetType(ibValueTypes::TYPE_NULL);
		return e;
	}

	// column path
	if (tk.m_kind == ibQueryTokenKind::Ident) {
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		e->m_line = tk.m_line; e->m_col = tk.m_col;
		e->m_path = ParseDottedName();
		return e;
	}

	// number / string / date constant
	if (tk.m_kind == ibQueryTokenKind::Number ||
	    tk.m_kind == ibQueryTokenKind::String ||
	    tk.m_kind == ibQueryTokenKind::Date) {
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		e->m_literal = tk.m_literal;
		e->m_line = tk.m_line; e->m_col = tk.m_col;
		++m_pos;
		return e;
	}

	// &parameter
	if (tk.m_kind == ibQueryTokenKind::Param) {
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
		e->m_paramName = tk.m_text;
		e->m_line = tk.m_line; e->m_col = tk.m_col;
		++m_pos;
		return e;
	}

	Fail(tk, _("expected a column, literal, or parameter"));
	return nullptr;   // unreachable — Fail always throws
}

ibQueryAstExprPtr ibQueryParser::ParseAggregate()
{
	const ibQueryToken& tk = Cur();
	const ibQueryKeyword fn = tk.m_keyword;
	++m_pos;

	ExpectPunct(wxT('('), wxT("'(' after an aggregate function"));

	auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Func);
	e->m_func = fn; e->m_line = tk.m_line; e->m_col = tk.m_col;

	if (Cur().IsOp(wxT("*"))) {
		++m_pos;
		if (fn != ibQueryKeyword::Count)
			Fail(tk, _("'*' is only valid as COUNT(*)"));
		e->m_star = true;
	}
	else {
		e->m_arg = ParseAddSub();   // a column path, or an arithmetic expression (SUM(Qty * Price))
	}

	ExpectPunct(wxT(')'), wxT("')'"));
	return e;
}
