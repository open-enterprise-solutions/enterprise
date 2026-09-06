////////////////////////////////////////////////////////////////////////////
//	L4-1 — text query language parser (queryParser.h)
////////////////////////////////////////////////////////////////////////////

#include "queryParser.h"

#include "queryException.h"   // ibBackendQuerySourceException — L4 refuses in its own variety

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
		ThrowQueryException(Cur(), wxString::Format(_("expected %s"), what));
}

bool ibQueryParser::AcceptPunct(wxChar c)
{
	if (Cur().IsPunct(c)) { ++m_pos; return true; }
	return false;
}

void ibQueryParser::ExpectPunct(wxChar c, const wxChar* what)
{
	if (!AcceptPunct(c))
		ThrowQueryException(Cur(), wxString::Format(_("expected %s"), what));
}

// L4's refusal, in L4's variety. The token carries the span, so it rides out as data and a consumer
// can put a caret on it rather than reading the number out of a translated sentence.
void ibQueryParser::ThrowQueryException(const ibQueryToken& at, const wxString& msg) const
{
	// ⚠ THE NUMBER A PERSON READS IS 1-BASED; the one that travels as DATA is not. The lexer converts
	// the line (`GetCurrentLine() + 1`) and leaves the column as the raw offset, which a caret wants —
	// so only the SENTENCE is shifted here. Printing the raw offset made every reported position one
	// character short of the token it named, which reads as "the parser is pointing at the space before
	// the problem" and costs whoever is reading the log an argument with their own arithmetic.
	ibBackendQuerySyntaxException::ErrorAt(at.m_line, at.m_col,
		_("Query syntax error at line %u (position %u): %s"), at.m_line, at.m_col + 1, msg);
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
		ThrowQueryException(Cur(), _("unexpected text after the query"));

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
		// ⭐⭐ A PACKAGE-LEVEL LINK, written where a statement would be:
		// `LINK Sales LEFT JOIN Plan ON …`.
		//
		// It relates the NAMED results of this package and belongs to the package, not to any
		// statement — nothing is added to anybody's FROM and nothing is materialised (Max,
		// 2026-08-21: mark two selections as named and set the links between them, and that is all).
		if (Cur().IsKeyword(ibQueryKeyword::Link)) {
			// …AND IT TAKES ITS PLACE IN THE SEQUENCE (Max, 2026-09-04: *"the idea of a package is the
			// SEQUENCE"*, and a link *"answers with how many rows it removed"*). So the section is a
			// STATEMENT as well as a set of relations: it runs where it stands, after the selections
			// it reconciles, and answers from its own position.
			//
			// ⭐ ONE STATEMENT PER SECTION, NOT PER RELATION — and the text is what settles it. A
			// chain is written with the head said ONCE (`LINK Sales LEFT JOIN Plan ON … JOIN Stock
			// ON …`), so it occupies one place in the sequence and reads back as one. Numbering
			// statements per relation made the count depend on how many JOINs a section happened to
			// carry, which is not something a reader of the text can see.
			//
			// The statement therefore carries the index of the section's FIRST relation; the rest of
			// the section follows it in m_links, sharing its head. See ibQueryAstStatement::m_linkIndex.
			{
				const int firstLink = static_cast<int>(package.m_links.size());

				for (ibQueryPackageLink& link : ParsePackageLinks())
					package.m_links.push_back(std::move(link));

				if (static_cast<int>(package.m_links.size()) > firstLink) {
					ibQueryAstStatement statement;
					statement.m_linkIndex = firstLink;
					package.m_statements.push_back(std::move(statement));
				}
			}
		}
		// ⚠ …AND THE FORM IT REPLACED IS REFUSED OUT LOUD. `JOIN A AND B ON …` standing here was the
		// first spelling (recognised by position, no keyword); read on today it would parse as a
		// statement that begins with JOIN, and the message would be about a missing SELECT. A person
		// with such a text is told what to write instead.
		else if (Cur().IsKeyword(ibQueryKeyword::Join)  || Cur().IsKeyword(ibQueryKeyword::Inner)
		      || Cur().IsKeyword(ibQueryKeyword::Left)  || Cur().IsKeyword(ibQueryKeyword::Right)
		      || Cur().IsKeyword(ibQueryKeyword::Full)) {
			ThrowQueryException(Cur(), _("a link between named results is written as "
				"LINK <name> JOIN <name> ON <condition>"));
		}
		else {
			package.m_statements.push_back(ParseStatement());
		}
		if (!AcceptPunct(wxT(';')))
			break;
		if (Cur().IsEnd())
			break;      // trailing ';' after the last statement
	}

	if (!Cur().IsEnd())
		ThrowQueryException(Cur(), _("unexpected text after the query"));

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
		ThrowQueryException(Cur(), _("unexpected text after the expression"));

	return expr;
}

// THE KIND OF A JOIN — `[INNER|LEFT|RIGHT|FULL] JOIN`, the one ladder both readers of it climb.
// False when the next token starts no join at all, which is how a loop over joins ends.
bool ibQueryParser::ParseJoinKind(ibQueryJoinKindAst& kind)
{
	if      (AcceptKw(ibQueryKeyword::Inner)) {                                   ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Inner; }
	else if (AcceptKw(ibQueryKeyword::Left))  { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Left;  }
	else if (AcceptKw(ibQueryKeyword::Right)) { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Right; }
	else if (AcceptKw(ibQueryKeyword::Full))  { AcceptKw(ibQueryKeyword::Outer); ExpectKw(ibQueryKeyword::Join, wxT("JOIN")); kind = ibQueryJoinKindAst::Full;  }
	else if (AcceptKw(ibQueryKeyword::Join))  {                                                                               kind = ibQueryJoinKindAst::Inner; }
	else
		return false;
	return true;
}

// ⭐⭐ A PACKAGE-LEVEL LINK SECTION — `LINK <name> [<kind>] JOIN <name> ON <condition> [ … ]`.
//
// Every name is one a statement gave its result with `ONTO`; each condition is an ordinary
// expression over the two sides' fields. THE RELATION IS WRITTEN THE WAY THIS LANGUAGE WRITES EVERY
// OTHER RELATION — the same kinds, the same `ON`, and a chain when there are several sources to
// relate. That is the whole reason the word in front exists (Max, 2026-08-27, on reading the old
// form): `JOIN A AND B ON …` needed the `AND` only because both names stood after one JOIN, and a
// package link therefore looked like nothing else in the language.
//
// ⚠ THE CHAIN IS FLATTENED INTO PAIRS, and no shape is lost by it: the FINAL query's FROM tree is
// built by the lowering out of these pairs, placed until nothing else can be placed
// (ExecutePackage). What each pair says is "these two are related, thus" — which is all the placer
// asks. The head is carried as the left of every link in the chain, so a later condition may name
// any selection already in it.
std::vector<ibQueryPackageLink> ibQueryParser::ParsePackageLinks()
{
	std::vector<ibQueryPackageLink> links;

	ExpectKw(ibQueryKeyword::Link, wxT("LINK"));

	if (Cur().m_kind != ibQueryTokenKind::Ident)
		ThrowQueryException(Cur(), _("expected the name of a selection after LINK"));
	const wxString head = Next().m_text;

	for (;;) {
		ibQueryPackageLink link;
		link.m_left = head;
		if (!ParseJoinKind(link.m_kind)) {
			if (links.empty())
				ThrowQueryException(Cur(), _("expected JOIN after the first selection of a LINK"));
			break;
		}

		if (Cur().m_kind != ibQueryTokenKind::Ident)
			ThrowQueryException(Cur(), _("expected the name of a selection after JOIN"));
		link.m_right = Next().m_text;

		// The condition is optional in the AST — a link may be declared and not written yet, which is
		// what the constructor's empty row is — but in TEXT it has to be there: a package that says two
		// selections are related without saying how says nothing.
		ExpectKw(ibQueryKeyword::On, wxT("ON"));
		link.m_on = ParsePredicate();
		links.push_back(std::move(link));
	}

	return links;
}

// One statement of a package. DROP releases a temp table the package made earlier; anything
// else is a SELECT (which may itself materialise INTO a temp table).
ibQueryAstStatement ibQueryParser::ParseStatement()
{
	ibQueryAstStatement statement;

	if (AcceptKw(ibQueryKeyword::Drop)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			ThrowQueryException(Cur(), _("expected a temporary-table name after DROP"));
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
			ThrowQueryException(n, _("expected a number after TOP"));
		sel->m_top = n.m_literal.GetInteger();
		if (sel->m_top <= 0)
			ThrowQueryException(n, _("TOP expects a positive row count"));
		++m_pos;
	}

	if (AcceptKw(ibQueryKeyword::Distinct)) sel->m_distinct = true;
	ParseSelectList(*sel);

	// INTO <name> — materialise this result as a temp table under that name instead of
	// returning it. Later statements of the package select FROM the bare name.
	if (AcceptKw(ibQueryKeyword::Into)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			ThrowQueryException(Cur(), _("expected a temporary-table name after INTO"));
		sel->m_intoTemp = Next().m_text;
	}

	// ONTO <name> — name the result this statement hands back. Written where INTO is, because the two
	// answer the same question ("what becomes of this result") and a reader should meet them in one
	// place. They are mutually exclusive: INTO hands back no result, so naming it would name nothing.
	if (AcceptKw(ibQueryKeyword::Onto)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident)
			ThrowQueryException(Cur(), _("expected a name after ONTO"));
		if (!sel->m_intoTemp.IsEmpty())
			ThrowQueryException(Cur(), _("INTO and ONTO cannot both be written: INTO puts the result in a temporary table and returns none, ONTO names the result it returns"));
		sel->m_ontoName = Next().m_text;
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
		const ibQueryToken& at = Cur();
		ExpectKw(ibQueryKeyword::By, wxT("BY"));
		ParseOrderBy(*sel);

		// ⭐ AND A TEMPORARY TABLE HAS NO ORDER EITHER — the twin of the TOTALS refusal below, and the
		// same reason underneath it (Max): what is materialised under a name is ROWS, and the
		// statements that read it afterwards select from a table. A table has no order to remember, so
		// an ORDER BY written here sorts something on its way into storage and is forgotten in the
		// same breath. Refusing beats sorting for nothing and letting the author believe otherwise.
		if (!sel->m_intoTemp.IsEmpty())
			ThrowQueryException(at, _("ORDER BY cannot be written with INTO: a temporary table keeps rows, not an order"));
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
			ThrowQueryException(at, _("TOTALS cannot be written with INTO: a temporary table is a flat table, "
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
			ThrowQueryException(at, _("INDEX BY needs INTO: only a temporary table this statement makes can be indexed"));
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
			ThrowQueryException(Cur(), _("a table passed in as a parameter can only be read INTO a temporary table: "
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
	//
	// ⭐ AFTER AN EXPLICIT `AS`, A KEYWORD IS A NAME — the same rule the dotted path already lives by
	// (`Document.Order`: after the dot, a word is a name whatever the table says). `AS` makes the
	// alias MANDATORY, so whatever follows it is being used as one, and there is nothing for the
	// word to be ambiguous with.
	//
	// Written the day `ROWS` joined the keyword table and `SELECT COUNT(*) AS rows` stopped parsing —
	// but the class is older and wider: `AS order`, `AS group`, `AS value`, `AS index` all failed the
	// same way, and every future keyword would silently claim another name people already use.
	//
	// WITHOUT the `AS` the rule cannot hold: the alias is optional there, so a keyword after the
	// expression is the NEXT CLAUSE (`SELECT Code FROM …`), and reading it as a name would swallow it.
	if (AcceptKw(ibQueryKeyword::As)) {
		if (Cur().m_kind != ibQueryTokenKind::Ident && Cur().m_kind != ibQueryTokenKind::Keyword)
			ThrowQueryException(Cur(), _("expected an alias name after AS"));
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

	// Taken before anything is consumed, so a source that turns out not to exist can be pointed at
	// where the author wrote it (see ibQuerySource::m_line).
	s.m_line = Cur().m_line;
	s.m_col  = Cur().m_col;

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
			ThrowQueryException(at, _("INTO belongs to a statement of a query package, not to a nested table"));
		if (s.m_subquery->m_forUpdate)
			ThrowQueryException(at, _("FOR UPDATE belongs to a statement, not to a nested table"));
		// ⭐ AND NEITHER TOTALS NOR ORDER BY BELONG TO A NESTED TABLE (Max). Totals are how a RESULT
		// is presented — a nested table is not a result, it is a source. An order inside a derived
		// table is promised by nothing in SQL and read by nobody; worse, writing one used to keep the
		// whole query in memory, because the subquery then could not collapse into its parent
		// (queryRewrite, rule 2) and its source stayed a RAM-computed wrapper.
		if (s.m_subquery->m_hasTotals)
			ThrowQueryException(at, _("TOTALS belongs to a statement's result, not to a nested table"));
		if (!s.m_subquery->m_orderBy.empty())
			ThrowQueryException(at, _("ORDER BY belongs to a statement, not to a nested table"));
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
			ThrowQueryException(Cur(), _("expected an alias name after AS"));
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
		if (!ParseJoinKind(kind))
			break;

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

		// ⭐ SORTING BY AN EXPRESSION — `ORDER BY CASE WHEN … END`, `ORDER BY Price * Qty`,
		// `ORDER BY ISNULL(Weight, 0)`. The tier below has carried this the whole time (the lowering's
		// OrderByExpr, "a CASE / arithmetic: sort by a condition"), and only the parser refused it,
		// with "unexpected text after the query" pointing at the word CASE — the engine disagreeing
		// with itself about what the language is (measured 2026-09-03).
		//
		// ⚠ AND A KEYWORD HERE IS STILL A NAME, which is why this is not simply ParsePredicate(). An
		// attribute called `Order`, `Value`, `Count` or `Group` must keep sorting the way it always
		// has — so only the words that can BEGIN AN EXPRESSION and could not be a bare name in this
		// position take the expression road: CASE, ISNULL, an aggregate call, and anything opening
		// with a bracket, a literal or a &parameter. Everything else is read as the dotted name it
		// has always been.
		const ibQueryToken& tk = Cur();
		const bool startsExpression =
			   (tk.m_kind == ibQueryTokenKind::Keyword
			     && (tk.m_keyword == ibQueryKeyword::Case
			      || tk.m_keyword == ibQueryKeyword::IsNull
			      || ibIsAggregateKeyword(tk.m_keyword)))
			|| tk.m_kind == ibQueryTokenKind::Number
			|| tk.m_kind == ibQueryTokenKind::String
			|| tk.m_kind == ibQueryTokenKind::Date
			|| tk.m_kind == ibQueryTokenKind::Param
			// A LEADING SIGN starts an expression too — `ORDER BY -Total` is "biggest first" written
			// the short way, and it reads as a name only until the minus is accounted for.
			|| (tk.m_kind == ibQueryTokenKind::Op && (tk.m_text == wxT("-") || tk.m_text == wxT("+")))
			|| (tk.m_kind == ibQueryTokenKind::Punct && tk.m_text == wxT("("));

		if (startsExpression) {
			it.m_expr = ParsePredicate();
		}
		else {
			it.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
			// ⚠ IN AN ORDER BY ITEM, A KEYWORD IS A NAME — the same rule that already holds after a `.`,
			// one position earlier. A configuration naming an attribute `Order`, `Value`, `Count` or
			// `Group` does not consult our keyword table first. Before this, an enumeration's own
			// `Order` attribute made `ORDER BY Order` a syntax error, and the list that asked simply
			// came back empty.
			it.m_expr->m_path = ParseDottedName(/*firstMayBeKeyword*/true);

			// ⭐⭐ …AND AN OPERATOR BEHIND THE NAME MEANS IT WAS AN EXPRESSION ALL ALONG.
			// `ORDER BY Price * Qty` begins with a name, so the test above sends it down the name
			// road — and the road used to end at the `*`, taking the whole query with it
			// ("unexpected text after the query", pointing three words past the problem).
			//
			// The name just read IS the left operand, so the arithmetic levels are re-entered with
			// it in hand. Deciding by the FIRST TOKEN was the mistake: the first token of an
			// expression and the first token of a name are the same token.
			if (Cur().IsOp(wxT("*")) || Cur().IsOp(wxT("/")) || Cur().IsOp(wxT("%")))
				it.m_expr = ParseMulDivFrom(it.m_expr);

			if (Cur().IsOp(wxT("+")) || Cur().IsOp(wxT("-")))
				it.m_expr = ParseAddSubFrom(it.m_expr);
		}
		if (AcceptKw(ibQueryKeyword::Desc))      it.m_ascending = false;
		else if (AcceptKw(ibQueryKeyword::Asc))  it.m_ascending = true;
		sel.m_orderBy.push_back(std::move(it));
	} while (AcceptPunct(wxT(',')));
}

// ONE FIELD OF A TOTALS LEVEL — the field itself and how it is READ. Its own function because the
// QUERY CONSTRUCTOR edits a level's fields as text and must read them back exactly as the language
// does: a form that parsed "the column part" with its own code would accept what the language
// refuses, and — as it did — silently drop what it did not know how to read.
ibQueryTotalField ibQueryParser::ParseTotalField()
{
	ibQueryTotalField f;
	f.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	// ⭐⭐ A FIELD MAY BE CALLED AFTER A KEYWORD, and here that is not ambiguity — it is an ATTRIBUTE
	// NAME. A constant's attribute is literally `Value`, and `VALUE` is a keyword of this language
	// (the `value()` metaobject constant), so `BY Value` died as "expected a name" while
	// `SELECT Constant1.Value` parsed — after a dot a keyword is already read as a name (Max,
	// 2026-08-27, screenshot).
	//
	// ⚠ EXCEPT THE WORDS THAT ARE SERVICE WORDS *HERE*. `Hierarchy`, `Elements`, `Periods` modify
	// the field that precedes them, `Split` / `Onto` / `As` / `Overall` structure the clause: read as
	// names they would silently change what an existing query means. Everything else is just a word
	// somebody named a column with.
	const ibQueryKeyword ahead = Cur().m_kind == ibQueryTokenKind::Keyword ? Cur().m_keyword : ibQueryKeyword::None;
	const bool serviceHere = ahead == ibQueryKeyword::Hierarchy || ahead == ibQueryKeyword::HierarchyOnly
	                      || ahead == ibQueryKeyword::Elements  || ahead == ibQueryKeyword::Periods
	                      || ahead == ibQueryKeyword::Split     || ahead == ibQueryKeyword::Onto
	                      || ahead == ibQueryKeyword::As        || ahead == ibQueryKeyword::Overall;
	f.m_expr->m_path = ParseDottedName(/*firstMayBeKeyword*/ !serviceHere);
	// The unfold is the FIELD's — a level may unfold one of its fields through a hierarchy and take
	// the next one flat.
	if (AcceptKw(ibQueryKeyword::HierarchyOnly))     f.m_unfold = ibQueryDimUnfold::HierarchyOnly;
	else if (AcceptKw(ibQueryKeyword::Hierarchy))    f.m_unfold = ibQueryDimUnfold::Hierarchy;
	else if (AcceptKw(ibQueryKeyword::Elements))     f.m_unfold = ibQueryDimUnfold::Elements;
	// …and PERIODS is read in the same place, for the same reason: it says how the field is READ.
	// `BY Period PERIODS(Month, &From, &To)` — the unit is a bare word (the vocabulary belongs to the
	// lowering, which already reads it for a register's Turnovers), and the two bounds are optional.
	else if (AcceptKw(ibQueryKeyword::Periods)) {
		ExpectPunct(wxT('('), wxT("'(' after PERIODS"));
		auto periods = std::make_shared<ibQueryTotalPeriods>();
		if (Cur().m_kind != ibQueryTokenKind::Ident && Cur().m_kind != ibQueryTokenKind::Keyword)
			ThrowQueryException(Cur(), _("expected the period unit (Day / Month / Quarter / Year ...) in PERIODS"));
		periods->m_unit = Next().m_text;
		// The bounds, when written. Either may be left out — an empty slot reads as "from the data",
		// the same way an omitted argument does in a virtual-table call.
		if (AcceptPunct(wxT(','))) {
			if (!Cur().IsPunct(wxT(',')) && !Cur().IsPunct(wxT(')')))
				periods->m_from = ParseAddSub();
			if (AcceptPunct(wxT(',')) && !Cur().IsPunct(wxT(')')))
				periods->m_to = ParseAddSub();
		}
		ExpectPunct(wxT(')'), wxT("')' after the arguments of PERIODS"));
		f.m_periods = std::move(periods);
	}
	return f;
}

// The same thing from a piece of TEXT — the constructor's cell, one field per call. Same lexer, same
// parser, same refusals; nothing about a level field is spelled twice.
ibQueryTotalField ibQueryParser::ParseTotalsField(const wxString& fieldText)
{
	ibQueryLexer lexer;
	m_toks = lexer.Tokenize(fieldText);
	m_pos  = 0;

	ibQueryTotalField f = ParseTotalField();
	if (!Cur().IsEnd())
		ThrowQueryException(Cur(), _("unexpected text after the level field"));
	return f;
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
				ThrowQueryException(Cur(), _("expected an aggregate function (SUM/COUNT/MIN/MAX/AVG) or BY in TOTALS"));
			ibQueryTotalAggregate resource;
			resource.m_expr = ParseAggregate(/*allowWindow*/false);   // OVER here names the AREA — read below
			// ⭐⭐ `OVER <level>` — over WHAT this figure is computed, read right after the call and
			// before the name, because it is part of what the figure IS rather than of what it is
			// called. A level of a branch is addressed through it: `OVER ByCharacteristic.Series`.
			//
			// ⚠ NOT the `OVER (…)` of a window: that one is a modifier of a call in the SELECTION and
			// partitions ROWS. Here we are past the ladder, among NODES, so what follows is the name
			// of a level — never a list of fields. Told apart by the very next token: a bracket is a
			// window, a name is a level, and a window is not legal in this position at all.
			// ⭐ ONE NAME, OR SEVERAL IN BRACKETS. The window in the constructor ticks groupings, and a
			// person may tick more than one — `OVER (Item, Warehouse)` is then the area, read exactly
			// as it is written. A single name needs no brackets, which is the common case and stays
			// short.
			if (AcceptKw(ibQueryKeyword::Over)) {
				const bool bracketed = AcceptPunct(wxT('('));
				// ⭐ A GROUPING MAY BE NAMED AFTER A KEYWORD — the same rule, and the same reason, as
				// a totals FIELD (see ParseTotalField): a constant's attribute is literally `Value`,
				// and `VALUE` is a word of this language. An area names a grouping, so it inherits the
				// rule; `OVER (Posted, Value)` died as "expected a name" without it (Max, live,
				// 2026-08-27). The words that structure the clause here are `AS` and `BY`.
				const auto nameHere = [this]() -> wxString {
					if (Cur().m_kind == ibQueryTokenKind::Ident)
						return Next().m_text;
					if (Cur().m_kind == ibQueryTokenKind::Keyword
					    && Cur().m_keyword != ibQueryKeyword::As && Cur().m_keyword != ibQueryKeyword::By)
						return Next().m_text;
					return wxString();
				};
				do {
					wxString one = nameHere();
					if (one.IsEmpty())
						ThrowQueryException(Cur(), _("expected a grouping name after OVER: the level this figure is computed over"));
					if (AcceptPunct(wxT('.'))) {      // <branch>.<level> — the branch qualifies the level
						const wxString level = nameHere();
						if (level.IsEmpty())
							ThrowQueryException(Cur(), _("expected a grouping name after the branch name in OVER"));
						one += wxT(".") + level;
					}
					if (!resource.m_scope.IsEmpty())
						resource.m_scope += wxT(", ");
					resource.m_scope += one;
				} while (bracketed && AcceptPunct(wxT(',')));
				if (bracketed)
					ExpectPunct(wxT(')'), wxT("')' closing the OVER list"));
			}
			// [AS] alias — the name the FIGURE answers to, written exactly where a level writes its
			// own (see the BY loop below): after the thing it names, with AS optional. One rule for
			// both halves of a TOTALS, because both are columns of the same result.
			if (AcceptKw(ibQueryKeyword::As)) {
				if (Cur().m_kind != ibQueryTokenKind::Ident)
					ThrowQueryException(Cur(), _("expected an alias name after AS"));
				resource.m_alias = Next().m_text;
			}
			else if (Cur().m_kind == ibQueryTokenKind::Ident) {
				resource.m_alias = Next().m_text;
			}
			sel.m_totalsAggregates.push_back(std::move(resource));
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

	// ⭐⭐ WHERE THE LADDER FORKS. `SPLIT` between two levels says the second one does not continue
	// the first — it opens a branch that folds the same rows its own way. The comma keeps its old
	// meaning exactly (one level after another), so every query written before this parses as it did.
	//
	// The branch's NAME is written where a statement writes one, at the end of what it names:
	// `SPLIT Item, Characteristic ONTO ByItem`. It lands on the level that OPENED the branch, which
	// is where the rest of the engine looks for it — and the head is remembered here rather than
	// searched for backwards later.
	// ⭐⭐ WHICH NODE THE LEVELS ARE LANDING ON. Null = the HIDDEN node every report has, whose levels
	// are `m_totalsBy`; a `SPLIT` opens a visible one and everything after it hangs on that until the
	// next `SPLIT`. One pointer, moved as the text is read forwards — which is exactly how the text
	// says it.
	// ⚠ AN INDEX, NOT A POINTER: opening the next node grows the vector, and a pointer taken into it
	// before that would be left dangling by the reallocation.
	int    nodeAt = wxNOT_FOUND;                   // wxNOT_FOUND = the hidden node
	bool   more = false;                           // …a comma continues the ladder, SPLIT opens a node

	// ⭐⭐ `SPLIT <name> BY <levels>` — the node is NAMED WHERE IT IS OPENED, and then its groupings
	// follow. Written the other way round (`SPLIT <levels> ONTO <name>`) a reader had to reach the
	// end of the ladder to learn whose block they had been reading, and a node with nothing on it yet
	// — a legitimate state while a query is being built — had nowhere to carry a name at all.
	//
	// ⚠ THE NAME IS OPTIONAL, and `BY` is what says it was left out: `SPLIT BY Characteristic` opens
	// a node nobody named. Nothing has to be guessed from what a word looks like — a keyword cannot
	// be a name here, so the two forms are told apart by the token that follows the word.
	//
	// An unnamed node folds exactly like a named one; it is simply read BY POSITION, which is all a
	// reader can ask of something with nothing to call it.
	const auto openNode = [this, &sel, &nodeAt]() {
		ibQueryTotalSplit node;
		if (Cur().m_kind == ibQueryTokenKind::Ident)
			node.m_name = Next().m_text;
		ExpectKw(ibQueryKeyword::By, wxT("BY after SPLIT"));
		sel.m_totalsSplits.push_back(std::move(node));
		nodeAt = static_cast<int>(sel.m_totalsSplits.size()) - 1;
	};

	// A QUERY MAY OPEN ONE AT ONCE — `BY SPLIT Item SPLIT Unit`, with nothing on the hidden node.
	// Then every node hangs off the grand total, which is the honest reading of "nothing in common".
	if (AcceptKw(ibQueryKeyword::Split))
		openNode();

	do {
		ibQueryTotalDim d;

		// ONE LEVEL, SEVERAL FIELDS — `BY (Partner, Contract)`. The bracket is what says "together":
		// outside it a comma has always separated LEVELS, and that reading is untouched. A single
		// field needs no bracket, so every query written before this parses exactly as it did.
		const bool bracketed = AcceptPunct(wxT('('));
		do {
			d.m_fields.push_back(ParseTotalField());
		} while (bracketed && AcceptPunct(wxT(',')));
		if (bracketed)
			ExpectPunct(wxT(')'), wxT("')' after the fields of one TOTALS level"));

		// [AS] alias — the name this LEVEL answers to. Written after the fields, because the unfold
		// belongs to a dimension and the alias belongs to the level they produce together.
		if (AcceptKw(ibQueryKeyword::As)) {
			if (Cur().m_kind != ibQueryTokenKind::Ident)
				ThrowQueryException(Cur(), _("expected an alias name after AS"));
			d.m_alias = Next().m_text;
		}
		else if (Cur().m_kind == ibQueryTokenKind::Ident) {
			d.m_alias = Next().m_text;
		}
		// ONTO THE NODE IN HAND — the visible one if a SPLIT opened it, the hidden one otherwise.
		if (nodeAt != wxNOT_FOUND) sel.m_totalsSplits[nodeAt].m_levels.push_back(std::move(d));
		else                       sel.m_totalsBy.push_back(std::move(d));

		// A COMMA CONTINUES THIS NODE'S LADDER; `SPLIT` OPENS THE NEXT NODE. Two ways to go on, and
		// they mean different things — which is exactly why the word exists (a bracket could not say
		// it: the brackets are taken by a level of several fields).
		more = AcceptPunct(wxT(','));
		if (!more && AcceptKw(ibQueryKeyword::Split)) {
			openNode();
			more = true;
		}
	} while (more);
}

std::vector<wxString> ibQueryParser::ParseDottedName(bool firstMayBeKeyword)
{
	std::vector<wxString> parts;
	if (Cur().m_kind != ibQueryTokenKind::Ident
	    && !(firstMayBeKeyword && Cur().m_kind == ibQueryTokenKind::Keyword))
		ThrowQueryException(Cur(), _("expected a name"));
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
			ThrowQueryException(Cur(), _("expected a name after '.'"));
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
	// ⭐ `<expr> [NOT] REFS <Kind>.<Name>` — the type TEST. Written where LIKE and IN are written,
	// because it is the same sort of thing: a predicate over one operand and a fixed right-hand
	// side. The right side is a TYPE NAME rather than a value, so it is read as a dotted name — the
	// same way CAST reads the type it narrows to, and refused here if it is a bare word (a type is
	// `Catalog.Goods`, and `Goods` alone would name a table nobody declared).
	if (AcceptKw(ibQueryKeyword::Refs)) {
		const ibQueryToken at = Cur();
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Refs);
		n->m_negated = negated; n->m_lhs = lhs;
		n->m_line = at.m_line; n->m_col = at.m_col;
		n->m_path = ParseDottedName(/*firstMayBeKeyword*/true);
		if (n->m_path.size() < 2)
			ThrowQueryException(at, _("REFS takes a type: <Kind>.<Name>, as CAST does"));
		return n;
	}
	if (AcceptKw(ibQueryKeyword::In)) {
		// ⭐⭐ «IN HIERARCHY» IS THE SAME OPERATOR, TOLD HOW FAR DOWN TO LOOK — and it is said in the
		// three words this language already has (TOTALS BY unfolds a dimension by the very same ones).
		// A second VENUE for one vocabulary, not a second vocabulary: a report and a filter that both
		// say "in hierarchy" have to mean the same thing by it.
		ibQueryDimUnfold unfold = ibQueryDimUnfold::Elements;
		if (AcceptKw(ibQueryKeyword::HierarchyOnly))    unfold = ibQueryDimUnfold::HierarchyOnly;
		else if (AcceptKw(ibQueryKeyword::Hierarchy))   unfold = ibQueryDimUnfold::Hierarchy;

		ExpectPunct(wxT('('), wxT("'(' after IN"));
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::In);
		n->m_negated = negated; n->m_lhs = lhs; n->m_unfold = unfold;
		if (unfold != ibQueryDimUnfold::Elements) {
			// ⚠ THE OPERAND IS ONE PARAMETER, AND ONLY A PARAMETER — the one way this differs from a
			// plain IN, and it is not a restriction anybody can lift later by being thorough. Expanding
			// a subtree means WALKING DOWN from the values, so they have to be in hand before the read
			// starts; a subquery is not in hand until it runs. (Admitting one would mean either a
			// recursive predicate the filter vocabulary cannot state or a recursive CTE the dialect
			// layer does not spell — a different arc.) A parameter holding a LIST is the ordinary way
			// to name several: the operand is one expression, the values in it may be many.
			const ibQueryToken at = Cur();
			ibQueryAstExprPtr operand = ParseAddSub();
			if (!operand || operand->m_kind != ibQueryAstExprKind::Param || Cur().IsPunct(wxT(',')))
				ThrowQueryException(at, _("IN HIERARCHY takes one &parameter: the values have to be in hand to walk down to what is subordinate to them"));
			n->m_list.push_back(operand);
		}
		else if (Cur().IsKeyword(ibQueryKeyword::Select))     // lhs IN (SELECT …) — subquery form
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
		ThrowQueryException(Cur(), _("expected LIKE / IN / BETWEEN after NOT"));

	// bare primary (a boolean column / a parenthesized predicate)
	return lhs;
}

// Arithmetic — standard precedence (+ - below * / %). The column-based L3 door does not execute a
// computed expression yet, so the lowering rejects an Arith node; the parser stays complete.
ibQueryAstExprPtr ibQueryParser::ParseAddSub()
{
	return ParseAddSubFrom(ParseMulDiv());
}

// ⭐⭐ THE SAME LEVEL, ENTERED WITH ITS LEFT OPERAND ALREADY READ. One caller has to: ORDER BY reads
// its item as a NAME first (a keyword there is an attribute name — `ORDER BY Order` must keep
// working), and only then discovers an operator behind it. Without this it stopped at the operator
// and the query died as "unexpected text after the query" — `ORDER BY Price * Qty` did not parse at
// all (measured in CI, 2026-09-04).
//
// Split rather than repeated: precedence is stated ONCE, here, and both entrances walk the same
// loop. A second copy of "* binds tighter than +" is a second copy that eventually disagrees.
ibQueryAstExprPtr ibQueryParser::ParseAddSubFrom(ibQueryAstExprPtr lhs)
{
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
	return ParseMulDivFrom(ParsePrimary());
}

ibQueryAstExprPtr ibQueryParser::ParseMulDivFrom(ibQueryAstExprPtr lhs)
{
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
// A SCALAR CALL — the second family of calls, read where a name stands before a `(`.
//
// ⭐ THE ARGUMENT COUNT IS THE TABLE'S ANSWER, not a number written here. Each call declares its own
// range beside its word (queryLexer.cpp), so `DATEADD` needing three is stated in ONE place — the
// same place the palette writes its skeleton from and the syntax helper reads its signature out of.
// A count checked here against a literal would be a second authority on the language, free to
// disagree with the first the day a call gains an optional argument (DATETIME already has four).
ibQueryAstExprPtr ibQueryParser::ParseScalarCall(ibQueryScalarFn fn)
{
	const ibQueryToken name = Cur();
	++m_pos;                                        // the name
	ExpectPunct(wxT('('), wxT("'(' after a function name"));

	auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::ScalarCall);
	e->m_scalar = fn;
	e->m_line = name.m_line; e->m_col = name.m_col;

	if (!Cur().IsPunct(wxT(')')))
		do { e->m_args.push_back(ParsePredicate()); } while (AcceptPunct(wxT(',')));

	ExpectPunct(wxT(')'), wxT("')'"));

	size_t least = 0, most = 0;
	if (ibQueryScalarFnArity(fn, least, most) && (e->m_args.size() < least || e->m_args.size() > most)) {
		const wxString word = ibQueryScalarFnText(fn);
		if (least == most)
			ThrowQueryException(name, wxString::Format(
				_("%s takes %u argument(s), %u given"), word,
				static_cast<unsigned>(least), static_cast<unsigned>(e->m_args.size())));
		else
			ThrowQueryException(name, wxString::Format(
				_("%s takes between %u and %u arguments, %u given"), word,
				static_cast<unsigned>(least), static_cast<unsigned>(most),
				static_cast<unsigned>(e->m_args.size())));
	}
	return e;
}

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

	// ⭐ A LEADING MINUS — `-Quantity`, `SELECT -Amount AS Refund`, `ORDER BY -Total`. It is how a
	// person writes "the other direction", and the grammar simply had no place for it: the parser
	// looked for a column, a literal or a parameter and answered "expected a column, literal, or
	// parameter" pointing at the minus (measured 2026-09-04). The workaround people find is
	// `0 - Quantity`, which works — and is exactly what this builds, so nothing downstream learns a
	// new node: the AST, the renderer, the lowering and all four drivers keep the one arithmetic
	// they already have. A leading PLUS is accepted and dropped, being a no-op said out loud.
	if (tk.m_kind == ibQueryTokenKind::Op && (tk.m_text == wxT("-") || tk.m_text == wxT("+"))) {
		const bool negate = tk.m_text == wxT("-");
		const unsigned int line = tk.m_line, col = tk.m_col;
		++m_pos;
		ibQueryAstExprPtr operand = ParsePrimary();
		if (!negate)
			return operand;
		auto zero = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		zero->m_literal = ibValue(0);
		zero->m_line = line; zero->m_col = col;
		auto neg = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		neg->m_arith = ibQueryArithOp::Sub;
		neg->m_lhs = zero; neg->m_rhs = operand;
		neg->m_line = line; neg->m_col = col;
		return neg;
	}

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

	// ranking function — ROW_NUMBER() / RANK() / DENSE_RANK() OVER (…)
	if (tk.m_kind == ibQueryTokenKind::Keyword && ibIsRankingKeyword(tk.m_keyword))
		return ParseRanking();

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

	// ⭐⭐ A SCALAR CALL IS RECOGNISED BY POSITION, NOT BY OWNING THE WORD.
	//
	// `YEAR`, `MONTH`, `DAY`, `TYPE` are words a configuration is entitled to use for its own
	// attributes, and a virtual table already spells its periodicity with them. So they are NOT in
	// the keyword table (queryKeywords.h says why at length): a name followed by `(` is a call —
	// nothing else can stand there, since a column is never invoked — and the very same name
	// standing alone is still the field it always was.
	if (tk.m_kind == ibQueryTokenKind::Ident && PeekIsPunct(1, wxT('('))) {
		const ibQueryScalarFn fn = ibFindQueryScalarFn(tk.m_text.Upper());
		if (fn != ibQueryScalarFn::None)
			return ParseScalarCall(fn);
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

	// ⭐⭐ AND WHATEVER KEYWORD IS STILL STANDING HERE IS A NAME.
	//
	// Every keyword that can BEGIN an expression has already been taken above — SELECT (subquery),
	// CASE, NOT, the aggregates, VALUE, CAST, TRUE / FALSE / NULL. One that reaches this line is in a
	// position where nothing but a column may stand, so there is nothing left for it to be, and the
	// question needs no list of exceptions to answer: the structure of this function has answered it.
	//
	// It is the same rule already written into ORDER BY items and into names after a `.`, arriving at
	// the third place that needed it. A configuration is entitled to call an attribute `Order`, `Value`
	// or `Count` — those are ordinary words, and OUR grammar is not a fact about the user's data. An
	// enumeration's own `Order` column made the list that selected it die on "expected a column,
	// literal, or parameter", and the form came back empty with no visible reason.
	if (tk.m_kind == ibQueryTokenKind::Keyword) {
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		e->m_line = tk.m_line; e->m_col = tk.m_col;
		e->m_path = ParseDottedName(/*firstMayBeKeyword*/true);
		return e;
	}

	ThrowQueryException(tk, _("expected a column, literal, or parameter"));
	return nullptr;   // unreachable — Fail always throws
}

ibQueryAstExprPtr ibQueryParser::ParseAggregate(bool allowWindow)
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
			ThrowQueryException(tk, _("'*' is only valid as COUNT(*)"));
		if (e->m_distinctArg)
			ThrowQueryException(tk, _("COUNT(DISTINCT *) has no meaning: name the field whose different values to count"));
		e->m_star = true;
	}
	else {
		e->m_arg = ParseAddSub();   // a column path, or an arithmetic expression (SUM(Qty * Price))
	}

	ExpectPunct(wxT(')'), wxT("')'"));
	if (allowWindow)
		ParseWindowSuffix(*e);   // …OVER (…) — the call folds a PARTITION instead of a group
	return e;
}

// ROW_NUMBER() / RANK() / DENSE_RANK() OVER ( … ) — a call that numbers rows rather than folding
// them. Three rules, all of them consequences of what such a call IS, and all three enforced here
// so the error names the rule rather than the SQL the engine would have failed to prepare:
// no argument, an OVER is mandatory, and no frame (there is nothing to fold over).
ibQueryAstExprPtr ibQueryParser::ParseRanking()
{
	const ibQueryToken& tk = Cur();
	const ibQueryKeyword fn = tk.m_keyword;
	++m_pos;

	ExpectPunct(wxT('('), wxT("'(' after a ranking function"));

	auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Func);
	e->m_func = fn; e->m_line = tk.m_line; e->m_col = tk.m_col;

	// ⭐⭐ THE KEY MAY BE THE ARGUMENT — `RANK(Turnover DESC)`.
	//
	// A rank needs an ORDER: without one there is nothing to be first at. Written the SQL way that
	// order lives inside `OVER (…)`, and the author has to spell two clauses to say one thing — while
	// the AREA is already said beside the cell, in "computed over". So the argument names what the
	// place is measured by, and the engine assembles the window out of the two halves (Max,
	// 2026-08-27: "can we do without OVER and ORDER BY altogether?").
	//
	// The long form still parses — `RANK() OVER (Item ORDER BY Turnover DESC)` — and is what a
	// hand-written query, a frame, or an order of several keys needs.
	if (!Cur().IsPunct(wxT(')'))) {
		auto window = std::make_shared<ibQueryAstWindow>();
		do {
			ibQueryOrderItem key;
			key.m_expr = ParseAddSub();
			if (AcceptKw(ibQueryKeyword::Desc))     key.m_ascending = false;
			else if (AcceptKw(ibQueryKeyword::Asc)) key.m_ascending = true;
			window->m_orderBy.push_back(key);
		} while (AcceptPunct(wxT(',')));
		e->m_over = window;   // the partition, if any, arrives from the area beside it
	}
	ExpectPunct(wxT(')'), wxT("')'"));

	// …AND THE LONG FORM, where it is written. It may add the partition (and, written by hand, the
	// order this call did not carry in its brackets).
	if (Cur().IsKeyword(ibQueryKeyword::Over)) {
		const std::vector<ibQueryOrderItem> already = e->m_over ? e->m_over->m_orderBy
		                                                        : std::vector<ibQueryOrderItem>();
		e->m_over.reset();
		ParseWindowSuffix(*e);
		if (e->m_over && e->m_over->m_orderBy.empty())
			e->m_over->m_orderBy = already;      // the argument's key, kept when OVER states none
	}

	if (e->m_over && e->m_over->m_frame != ibQueryAstFrame::Unstated)
		ThrowQueryException(tk, _("a ranking function takes no ROWS / RANGE: it numbers rows, it does not fold them"));
	if (!e->m_over || e->m_over->m_orderBy.empty())
		ThrowQueryException(tk, _("a ranking function needs the field its place is measured by: RANK(Field DESC), or an ORDER BY inside OVER"));
	return e;
}

// The OVER suffix, shared by both kinds of call. Absent = an ordinary aggregate, and that is the
// common case — so this reads one token and leaves.
//
//   OVER ( [PARTITION BY expr, …] [ORDER BY expr [ASC|DESC], …] [ROWS|RANGE] )
//
// ⚠ THE FRAME IS ONE WORD. SQL writes the boundaries out (`ROWS BETWEEN UNBOUNDED PRECEDING AND
// CURRENT ROW`) and this grammar does not, because the engine offers those two frames and no
// others; accepting the long form would let someone write boundaries that quietly become different
// ones. What ROWS and RANGE mean is stated once, in queryKeywords.h.
void ibQueryParser::ParseWindowSuffix(ibQueryAstExpr& call)
{
	// ⭐⭐ `OVER` MEANS TWO DIFFERENT THINGS, AND THE NEXT TOKEN SAYS WHICH.
	//
	// A BRACKET opens a window over ROWS — this suffix, a modifier of a call in the selection. A NAME
	// is the AREA of a TOTALS figure (`TOTALS SUM(x) OVER Item`), which is read one storey up, among
	// nodes, by whoever is parsing the totals.
	//
	// So a name is not ours and must be left where it stands: swallowing the word here and then
	// demanding a bracket refused the very query the constructor writes (Max, live, 2026-08-27:
	// "expected '(' after OVER" on `COUNT(Number) OVER Posted`).
	if (!Cur().IsKeyword(ibQueryKeyword::Over))
		return;
	// ⚠ AND THE STOREY IS SETTLED BY THE CALLER, not guessed from what follows: this suffix is only
	// reached from the SELECTION (ParseAggregate's `allowWindow`), where `OVER` always opens a window.
	// In TOTALS the same word names the figure's AREA and is read there.
	if (!Peek().IsPunct(wxT('(')))
		return;                                  // `OVER <name>` is not a window — leave the word alone
	Next();                                      // …and only now is the word ours

	const ibQueryToken& open = Cur();
	ExpectPunct(wxT('('), wxT("'(' after OVER"));

	auto window = std::make_shared<ibQueryAstWindow>();

	// ⭐⭐ `PARTITION BY` IS OPTIONAL — everything before `ORDER BY` inside the bracket IS the
	// partition, and saying so twice adds nothing:
	//
	//     SUM(Amount) OVER (Item)                      -- the share's denominator
	//     RANK()      OVER (Item ORDER BY Amount DESC) -- the place within an item
	//     SUM(Amount) OVER (Item ORDER BY Date ROWS)   -- running, within an item
	//
	// The full SQL spelling still parses — somebody arriving from SQL writes what they know and gets
	// the same query. This is the same economy the totals' area already makes: the words that carry
	// no meaning of their own are the ones to drop.
	if (AcceptKw(ibQueryKeyword::Partition)) {
		ExpectKw(ibQueryKeyword::By, wxT("BY after PARTITION"));
		do { window->m_partitionBy.push_back(ParseAddSub()); } while (AcceptPunct(wxT(',')));
	}
	else if (!Cur().IsKeyword(ibQueryKeyword::Order) && !Cur().IsPunct(wxT(')'))) {
		// A bare list — the short form. `OVER ()` stays legitimate and means the whole result.
		do { window->m_partitionBy.push_back(ParseAddSub()); } while (AcceptPunct(wxT(',')));
	}

	if (AcceptKw(ibQueryKeyword::Order)) {
		ExpectKw(ibQueryKeyword::By, wxT("BY after ORDER"));
		do {
			ibQueryOrderItem item;
			item.m_expr = ParseAddSub();
			if (AcceptKw(ibQueryKeyword::Desc))     item.m_ascending = false;
			else if (AcceptKw(ibQueryKeyword::Asc)) item.m_ascending = true;
			window->m_orderBy.push_back(item);
		} while (AcceptPunct(wxT(',')));
	}

	if (AcceptKw(ibQueryKeyword::Rows))       window->m_frame = ibQueryAstFrame::Rows;
	else if (AcceptKw(ibQueryKeyword::Range)) window->m_frame = ibQueryAstFrame::Range;

	// A FRAME WITHOUT AN ORDER IS ABOUT NOTHING, and silently ignoring it is how a reader comes to
	// believe a query says something it does not.
	if (window->m_frame != ibQueryAstFrame::Unstated && window->m_orderBy.empty())
		ThrowQueryException(open, _("ROWS / RANGE needs an ORDER BY inside the OVER: a frame is a position in an order"));

	ExpectPunct(wxT(')'), wxT("')' closing OVER"));
	call.m_over = window;
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
