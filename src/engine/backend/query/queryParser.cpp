////////////////////////////////////////////////////////////////////////////
//	L4-1 — text query language parser (queryParser.h)
////////////////////////////////////////////////////////////////////////////

#include "queryParser.h"

#include "backend/backend_exception.h"   // ibBackendCoreException

namespace {

// A TOKEN that is an aggregate call — the keyword question is the keyword table's
// (ibIsAggregateKeyword); this only says "and it is a keyword at all".
bool IsAggregateKw(const ibQueryToken& t)
{
	return t.m_kind == ibQueryTokenKind::Keyword && ibIsAggregateKeyword(t.m_keyword);
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

	AcceptPunct(wxT(';'));   // a lone trailing separator is punctuation, not a second statement

	if (!Cur().IsEnd())
		Fail(Cur(), _("unexpected text after the query"));

	return sel;
}

ibQueryPackage ibQueryParser::ParsePackage(const wxString& queryText)
{
	ibQueryLexer lexer;
	m_toks = lexer.Tokenize(queryText);
	m_pos = 0;

	ibQueryPackage package;

	// Empty text is an empty package, not an error: the constructor opens on nothing
	// and builds from nothing, which is the same road as building from something.
	if (Cur().IsEnd())
		return package;

	for (;;) {
		package.m_statements.push_back(ParseStatement());
		if (!AcceptPunct(wxT(';')))
			break;
		if (Cur().IsEnd())
			break;      // trailing ';' after the last statement
	}

	if (!Cur().IsEnd())
		Fail(Cur(), _("unexpected text after the query"));

	return package;
}

ibQueryAstExprPtr ibQueryParser::ParseExpression(const wxString& exprText)
{
	ibQueryLexer lexer;
	m_toks = lexer.Tokenize(exprText);
	m_pos = 0;

	if (Cur().IsEnd())
		return nullptr;

	ibQueryAstExprPtr expr = ParsePredicate();

	if (!Cur().IsEnd())
		Fail(Cur(), _("unexpected text after the expression"));

	return expr;
}

// One statement of a package. DROP releases a temp table the package made earlier; anything
// else is a SELECT (which may itself materialise INTO a temp table).
ibQueryAstStatement ibQueryParser::ParseStatement()
{
	ibQueryAstStatement statement;

	if (AcceptKw(ibQueryKeyword::Drop)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			Fail(Cur(), _("expected a temporary-table name after DROP"));
		statement.m_dropTemp = Next().m_text;
		return statement;
	}

	statement.m_select = ParseSelectStatement();
	return statement;
}

// One SELECT BODY up to HAVING (no ORDER / TOTALS) — a single branch of a (possible) UNION.
ibQuerySelectPtr ibQueryParser::ParseSelectCore()
{
	auto sel = std::make_shared<ibQuerySelect>();

	ExpectKw(ibQueryKeyword::Select, wxT("SELECT"));

	// SELECT ALLOWED — read what this user MAY read and skip the rest, instead of refusing the
	// whole query. First, so the modifiers read in the order the renderer writes them.
	if (AcceptKw(ibQueryKeyword::Allowed)) sel->m_allowed = true;

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

	// INTO <name> — materialise this result as a temp table under that name instead of
	// returning it. Later statements of the package select FROM the bare name.
	if (AcceptKw(ibQueryKeyword::Into)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			Fail(Cur(), _("expected a temporary-table name after INTO"));
		sel->m_intoTemp = Next().m_text;
	}

	// FROM IS OPTIONAL. `SELECT 1` is a legitimate query: it returns one row carrying the value 1,
	// and it is how a constant, a parameter or a computed expression is asked for without touching a
	// table. Requiring a source made the constructor produce `FROM` over nothing while a query was
	// still being built — and made a whole legitimate shape of the language unwritable.
	//
	// The lowering answers a source-less select by evaluating the projections and handing back that
	// single row (ibQueryLowering::ExecuteImpl); a COLUMN in one is refused there, because there is
	// nothing to read it from.
	if (AcceptKw(ibQueryKeyword::From)) {
		sel->m_from = ParseSource();
		ParseJoins(*sel);
	}

	if (AcceptKw(ibQueryKeyword::Where))
		sel->m_where = ParsePredicate();

	if (AcceptKw(ibQueryKeyword::Group)) {
		ExpectKw(ibQueryKeyword::By, wxT("BY"));
		do { sel->m_groupBy.push_back(ParsePrimary()); } while (AcceptPunct(wxT(',')));
	}

	// ⚠ HAVING IS ITS OWN CLAUSE, not a tail of GROUP BY. Nested inside it, `SELECT … FROM …
	// HAVING SUM(x) = &v` — no grouping — died as "unexpected text after the query", pointing at a
	// keyword the reader can plainly see.
	//
	// And it is not a corner: with no GROUP BY the WHOLE RESULT IS ONE GROUP, which is exactly what
	// `TOTALS … BY OVERALL` says in the other clause. The constructor produces this shape by itself
	// now — a condition over a folded value MOVES here from WHERE — so a parser that could not read
	// it made the window generate text its own engine refused.
	if (AcceptKw(ibQueryKeyword::Having))
		sel->m_having = ParsePredicate();

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

	if (AcceptKw(ibQueryKeyword::Totals)) {
		const ibQueryToken& at = Cur();
		ParseTotals(*sel);

		// A TEMPORARY TABLE IS A FLAT TABLE, and TOTALS produces a TREE. There is nowhere for the
		// subtotal levels to go: what is materialised under a name is rows, and the statements that
		// read it afterwards select from a table, not from a hierarchy. Saying so here — rather than
		// dropping the levels quietly on the way in — is the difference between a refusal and a
		// query that returns something the author did not ask for.
		if (!sel->m_intoTemp.IsEmpty())
			Fail(at, _("TOTALS cannot be written with INTO: a temporary table is a flat table, "
			           "and TOTALS yields a tree"));
	}

	// INDEX BY … — the columns the temp table this statement makes is indexed by.
	if (AcceptKw(ibQueryKeyword::Index)) {
		const ibQueryToken& at = Cur();
		ExpectKw(ibQueryKeyword::By, wxT("BY after INDEX"));
		do {
			auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
			column->m_path = ParseDottedName();
			sel->m_indexBy.push_back(column);
		} while (AcceptPunct(wxT(',')));

		// AN INDEX ON NOTHING IS NOTHING. Without INTO there is no table being made, so the clause
		// could only describe a table that does not exist — a silent no-op, and the loudest kind of
		// wrong in a query somebody wrote for speed.
		if (sel->m_intoTemp.IsEmpty())
			Fail(at, _("INDEX BY needs INTO: only a temporary table this statement makes can be indexed"));
	}

	// ⭐ A TABLE HANDED IN GOES INTO A TEMPORARY TABLE, AND ONLY THERE. `FROM &Goods` is legal in a
	// statement that writes `INTO`, and nowhere else:
	//
	//     SELECT * INTO Goods FROM &GoodsTable;      -- materialise it, once
	//     SELECT … FROM Catalog.Products JOIN Goods … -- and from here it is an ordinary table
	//
	// The reason is not ceremony. A value table lives in RAM: every query that names it directly
	// forces the read to be stitched in memory, and it is stitched AGAIN for every statement that
	// mentions it. Materialised once into a temp table, it is a table the engine can promote and
	// JOIN server-side — so the discipline is what makes the rest of the package fast, and the
	// refusal is what stops somebody paying the RAM cost five times without knowing.
	//
	// Refused HERE, at the parse, so the constructor's live check says it the moment it is typed.
	{
		auto handedIn = [](const ibQuerySource& source) { return source.m_parameter; };
		bool usesParameter = handedIn(sel->m_from);
		for (const ibQueryAstJoin& join : sel->m_joins)
			usesParameter = usesParameter || handedIn(join.m_source);
		for (const ibQuerySelectPtr& branch : sel->m_unions)
			if (branch)
				usesParameter = usesParameter || handedIn(branch->m_from);

		if (usesParameter && sel->m_intoTemp.IsEmpty())
			Fail(Cur(), _("a table passed in as a parameter can only be read INTO a temporary table: "
			              "write `SELECT * INTO <name> FROM &<parameter>` first, then read that name"));
	}

	// FOR UPDATE — last, because it qualifies the whole statement: the rows this select
	// returned are HELD until the transaction ends.
	if (AcceptKw(ibQueryKeyword::For)) {
		ExpectKw(ibQueryKeyword::Update, wxT("UPDATE after FOR"));
		sel->m_forUpdate = true;
	}

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
	// A PROJECTION IS AN EXPRESSION — the AST has always said so and the renderer has always written
	// one, so the parser reading only up to arithmetic was a hole in the ROUND TRIP: the constructor
	// could produce `SELECT DeletionMark IS NULL` (its expression editor writes any expression into
	// a field) and then fail to read its own text back. A boolean-valued column is an ordinary thing
	// to select, and what cannot be EXECUTED is still refused — by the lowering, in its own words,
	// which is the layer whose job that is.
	//
	// ⚠⚠ AND AN AGGREGATE IS AN EXPRESSION TOO. This used to branch — an aggregate keyword went
	// straight to ParseAggregate — which read the CALL and stopped, so `SUM(Qty) / COUNT(*) * 1.2`
	// died as "unexpected text after the query" at the slash. A ratio of two folds is one of the
	// most ordinary things a report asks for, and the language could not say it.
	//
	// The branch was never needed: ParsePrimary already routes an aggregate keyword to
	// ParseAggregate, so the ordinary expression grammar reads `SUM(x)` and `SUM(x)/COUNT(y)` alike.
	// A special case for "starts with" is almost always a parser deciding a shape by its first token
	// instead of parsing it.
	p.m_expr = ParsePredicate();

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
		const ibQueryToken& at = Cur();
		++m_pos;                                   // '('
		s.m_subquery = ParseSelectStatement();
		ExpectPunct(wxT(')'), wxT("')' after a subquery"));
		// A NESTED QUERY IS NOT A STATEMENT. INTO materialises a package's temp table and FOR
		// UPDATE holds a statement's rows — inside a source both are silent no-ops, and a silent
		// no-op in a query someone wrote deliberately is worse than a refusal.
		if (!s.m_subquery->m_intoTemp.IsEmpty())
			Fail(at, _("INTO belongs to a statement of a query package, not to a nested table"));
		if (s.m_subquery->m_forUpdate)
			Fail(at, _("FOR UPDATE belongs to a statement, not to a nested table"));
	}
	// A TABLE HANDED IN: `FROM &Goods`. The `&` is the same mark a value parameter carries, and it
	// says the same thing — this came from outside the query. Said at the point of use, so a reader
	// never has to know which registry a bare name would have been looked up in.
	else if (Cur().m_kind == ibQueryTokenKind::Param) {
		s.m_parameter = true;
		s.m_name.push_back(Next().m_text);
	}
	else {
		s.m_name = ParseDottedName();

		// optional source-call args — `Balance(&Period, Warehouse = &Store)`: the arguments a virtual
		// table is built from (its as-of moment, its condition), handed to CreateQueryable.
		//
		// ⚠ A FULL EXPRESSION PER ARGUMENT, not a primary. An argument is not always a bare `&name`:
		// a moment can be computed (`BegOfMonth(&Date)`) and a CONDITION is a predicate
		// (`Warehouse = &Store AND Item = &Item`). Reading only a primary swallowed the first token
		// and then demanded `)` — `SliceLast(Resource2 = &Resource2)` failed at the `=`, complaining
		// about a bracket, which points at the wrong end of the problem entirely.
		//
		// The comma stays the separator: it is not an operator in this language, so the expression
		// parser stops at it on its own.
		if (AcceptPunct(wxT('('))) {
			if (!Cur().IsPunct(wxT(')'))) {
				// AN OMITTED ARGUMENT KEEPS ITS PLACE — `Balance(, Warehouse = &W)` means "no moment,
				// this condition", and `Turnovers(&From, &To, , Warehouse = &W)` skips only the
				// periodicity. The arguments are positional, so a missing one has to be written as
				// nothing between commas; reading it as absent would shift every later argument into
				// the wrong slot.
				do {
					if (Cur().IsPunct(wxT(',')) || Cur().IsPunct(wxT(')')))
						s.m_args.push_back(nullptr);
					else
						s.m_args.push_back(ParsePredicate());
				} while (AcceptPunct(wxT(',')));
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
		// ⭐⭐ A COMMA IS THE PRODUCT — `FROM A, B`, the oldest way there is of saying "both tables,
		// nothing said about how they meet". It parses to a join with NO condition, which is exactly
		// what the lowering already reads as a cross join.
		//
		// It exists so that JOIN never has to mean it. Written as `JOIN B` with nothing after it, "no
		// link" and "a link somebody stopped writing" are the same text, and the reader cannot tell
		// them apart — which is why a bare JOIN is refused below. The comma says the first out loud;
		// JOIN then always carries its ON.
		if (Cur().IsPunct(wxT(','))) {
			++m_pos;
			ibQueryAstJoin product;
			product.m_kind = ibQueryJoinKindAst::Inner;
			product.m_source = ParseSource();
			sel.m_joins.push_back(std::move(product));
			continue;
		}

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
		// ⚠ AND A JOIN ALWAYS CARRIES ITS CONDITION. `JOIN B` with nothing after it reads as a
		// sentence somebody stopped writing, and it used to be accepted — silently meaning the
		// product, which the comma above now says properly. Whoever wants every combination writes
		// the comma, or `ON TRUE`; whoever forgot the keys is told, here, at the position of the
		// table they forgot them on.
		ExpectKw(ibQueryKeyword::On, wxT("ON after JOIN (use a comma for every combination of rows)"));
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

	// OVERALL — the level above every dimension, and the only one that names no column. It is read
	// where it is WRITTEN (first in the BY list), and it is not a dimension: it takes no unfold and
	// no alias, because there is nothing to unfold and one row cannot need telling apart.
	if (AcceptKw(ibQueryKeyword::Overall)) {
		sel.m_totalsOverall = true;
		// `BY OVERALL` alone is a whole totals query — one row over everything. The comma is what
		// says dimensions follow.
		if (!AcceptPunct(wxT(',')))
			return;
	}

	do {
		ibQueryTotalDim d;
		d.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		d.m_expr->m_path = ParseDottedName();
		if (AcceptKw(ibQueryKeyword::HierarchyOnly))     d.m_unfold = ibQueryDimUnfold::HierarchyOnly;
		else if (AcceptKw(ibQueryKeyword::Hierarchy))    d.m_unfold = ibQueryDimUnfold::Hierarchy;
		else if (AcceptKw(ibQueryKeyword::Elements))     d.m_unfold = ibQueryDimUnfold::Elements;

		// [AS] alias — the name this LEVEL answers to. Written after the unfold, because the unfold
		// belongs to the dimension and the alias belongs to the level it produces.
		if (AcceptKw(ibQueryKeyword::As)) {
			if (Cur().m_kind != ibQueryTokenKind::Ident)
				Fail(Cur(), _("expected an alias name after AS"));
			d.m_alias = Next().m_text;
		}
		else if (Cur().m_kind == ibQueryTokenKind::Ident) {
			d.m_alias = Next().m_text;
		}
		sel.m_totalsBy.push_back(std::move(d));
	} while (AcceptPunct(wxT(',')));
}

std::vector<wxString> ibQueryParser::ParseDottedName(bool firstMayBeKeyword)
{
	std::vector<wxString> parts;
	if (Cur().m_kind != ibQueryTokenKind::Ident
	    && !(firstMayBeKeyword && Cur().m_kind == ibQueryTokenKind::Keyword))
		Fail(Cur(), _("expected a name"));
	parts.push_back(Next().m_text);
	while (AcceptPunct(wxT('.'))) {
		// ⚠ AFTER A DOT, A KEYWORD IS A NAME. The position decides: nothing but a name can follow a
		// `.`, so there is no ambiguity to resolve and no reason to refuse one.
		//
		// It is not a corner. `Document.Order` is an entirely ordinary metaobject — and `Order`,
		// `Group`, `Index`, `Value`, `Update`, `Elements`, `Count` are all words this language
		// reserves. A configuration names its documents and its attributes; it does not consult our
		// keyword table first, and it should not have to. Before this, `CAST(Recorder AS
		// Document.Order)` died as "expected a name after '.'", pointing at a word plainly there.
		if (Cur().m_kind != ibQueryTokenKind::Ident && Cur().m_kind != ibQueryTokenKind::Keyword)
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

// ⭐ `ISNULL(a, b)` — "a, and b where a is nothing". Read as the CASE it is:
//
//     CASE WHEN a IS NULL THEN b ELSE a END
//
// Nothing below the parser learns a new node: the lowering, every provider and every dialect already
// carry CASE, so the substitution works the day it is written — including inside an aggregate, a
// condition or a nested table.
//
// ⚠ AND THE SPELLING DOES NOT SURVIVE THE ROUND TRIP: written `ISNULL`, it comes back as the CASE.
// That is the price of not adding a node, and it is a real one — a reader who wrote the short form
// finds the long one. Keeping the word would mean its own AST kind, its own rendering and its own
// lowering; worth doing when the short form earns its keep, not before.
ibQueryAstExprPtr ibQueryParser::ParseIsNullCall()
{
	const ibQueryToken kw = Cur();
	ExpectKw(ibQueryKeyword::IsNull, wxT("ISNULL"));
	ExpectPunct(wxT('('), wxT("'(' after ISNULL"));
	ibQueryAstExprPtr value = ParsePredicate();
	ExpectPunct(wxT(','), wxT("',' - ISNULL takes the value and what to use when it is null"));
	ibQueryAstExprPtr fallback = ParsePredicate();
	ExpectPunct(wxT(')'), wxT("')' after ISNULL"));

	auto isNull = ibQueryAstExpr::Make(ibQueryAstExprKind::IsNull);
	isNull->m_lhs = value;
	isNull->m_line = kw.m_line; isNull->m_col = kw.m_col;

	auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Case);
	n->m_line = kw.m_line; n->m_col = kw.m_col;
	n->m_cases.emplace_back(isNull, fallback);
	n->m_else = value;
	return n;
}

ibQueryAstExprPtr ibQueryParser::ParsePrimary()
{
	const ibQueryToken& tk = Cur();

	if (tk.IsKeyword(ibQueryKeyword::IsNull))
		return ParseIsNullCall();

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

	// value(<Kind>.<Name>.<Member>) — a literal reference constant (empty ref / predefined), resolved at lowering
	if (tk.IsKeyword(ibQueryKeyword::Value))
		return ParseValueConstant();

	// CAST(<expr> AS <Kind>.<Name>) [ . field … ] — narrow a composite reference so it can be walked.
	if (tk.IsKeyword(ibQueryKeyword::Cast))
		return ParseCast();

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

	// DISTINCT INSIDE THE CALL — `COUNT(DISTINCT Board)`. Read before the argument because that is
	// where SQL puts it, and it belongs to THIS call: the statement's own DISTINCT is a different
	// question (whole rows, not one column's values).
	if (AcceptKw(ibQueryKeyword::Distinct))
		e->m_distinctArg = true;

	if (Cur().IsOp(wxT("*"))) {
		++m_pos;
		if (fn != ibQueryKeyword::Count)
			Fail(tk, _("'*' is only valid as COUNT(*)"));
		if (e->m_distinctArg)
			Fail(tk, _("COUNT(DISTINCT *) has no meaning: name the field whose different values to count"));
		e->m_star = true;
	}
	else {
		e->m_arg = ParseAddSub();   // a column path, or an arithmetic expression (SUM(Qty * Price))
	}

	ExpectPunct(wxT(')'), wxT("')'"));
	return e;
}

// VALUE( <Kind>.<Name>.<Member> ) — a LITERAL reference constant: the empty reference of a metaobject
// (`value(Catalog.Currencies.EmptyRef)`) or one of its predefined items (`value(Catalog.Currencies.Dollar)`).
// The name is NOT resolved here (the AST is metadata-free) — the dotted path is carried as-is and resolved at
// lowering, where the config is in scope. The resolved value then flows as a bound
// value, exactly like a &parameter.
ibQueryAstExprPtr ibQueryParser::ParseValueConstant()
{
	const ibQueryToken& tk = Cur();
	++m_pos;

	ExpectPunct(wxT('('), wxT("'(' after VALUE"));

	auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Value);
	e->m_line = tk.m_line; e->m_col = tk.m_col;
	e->m_path = ParseDottedName();

	ExpectPunct(wxT(')'), wxT("')'"));
	return e;
}

// CAST( <expr> AS <Kind>.<Name> ) [ . field … ]
//
// ⚠ THE TRAILING PATH IS THE WHOLE POINT, and it decides the shape. `CAST(x AS T)` on its own is a
// Cast node; `CAST(x AS T).A.B` is a COLUMN whose path is {A, B} and whose root (m_arg) is the cast.
//
// That is not a trick — it is what the construction MEANS. A cast says which of a composite
// reference's types is meant; what you do with it is walk into that type. Making the walk an
// ordinary Column means every mechanism that already resolves a dot-walk resolves this one too,
// with the cast supplying only the queryable the walk starts on.
ibQueryAstExprPtr ibQueryParser::ParseCast()
{
	const ibQueryToken& tk = Cur();
	++m_pos;

	ExpectPunct(wxT('('), wxT("'(' after CAST"));

	auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
	cast->m_line = tk.m_line; cast->m_col = tk.m_col;
	cast->m_arg = ParsePredicate();

	ExpectKw(ibQueryKeyword::As, wxT("AS in CAST"));
	// THE TARGET TYPE, as the language names a source: `Document.Order`. Written the same way a FROM
	// writes it, because it IS the same thing — the table whose fields the walk continues into.
	cast->m_path = ParseDottedName();
	ExpectPunct(wxT(')'), wxT("')' after the cast type"));

	// ⚠ THE DOT IS CONSUMED, not merely looked at. Peeking and then calling ParseDottedName left the
	// `.` as the current token, and that reader wants a NAME first — so every `CAST(x AS T).Field`
	// died as "expected a name", pointing at the dot it had just recognised.
	if (!AcceptPunct(wxT('.')))
		return cast;

	auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	column->m_line = tk.m_line; column->m_col = tk.m_col;
	column->m_arg  = cast;              // the walk starts HERE, not on a source alias
	column->m_path = ParseDottedName(/*firstMayBeKeyword*/true); // …and continues through these
	return column;
}
