// THE ROUND TRIP AS A PROPERTY, not as a list of examples.
//
// `tests/test_queryL4Parser.cpp` pins ~36 hand-written queries, and every one of them passes. What
// they cannot do is find the shape NOBODY THOUGHT TO WRITE — and on 2026-08-07 the first suite run
// in three arcs turned up three defects that were exactly that:
//
//   * `Document.Order` did not parse — `Order` is a keyword, and after a `.` the reader wanted an
//     identifier;
//   * `SUM(x) / COUNT(y)` did not parse — a projection starting with an aggregate read only the
//     call and stopped;
//   * `CAST(x AS T).Field` never worked — the dot was recognised and not consumed.
//
// All three sat in the READER while the WRITER was complete, which is the standing asymmetry of
// this pair: the renderer runs on every screen refresh of the constructor, the parser only on text
// that actually takes a given shape. So the shapes nobody generates stay broken silently — until
// something starts generating them, which is precisely what the constructor now does.
//
// This file generates them on purpose. A deterministic walk over the AST builds queries, and the
// property is the one the whole constructor rests on:
//
//     render(parse(render(ast))) == render(ast)
//
// Rendering both sides is deliberate: there is no deep equality on the AST, and text is the
// contract anyway — the window shows it, the engine reads it, and a query that survives one trip
// but changes on the second is exactly a query the constructor would corrupt on a second open.
//
// ⚠ SEEDED, NOT RANDOM. A failure has to be reproducible and its input printable, so the generator
// is a plain LCG with a fixed seed and every failure prints the query that caused it. A test that
// fails differently each run is a test people learn to re-run.

#include <gtest/gtest.h>

#include "backend/query/queryAst.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/backend_exception.h"

namespace {

// ---------------------------------------------------------------------------
//  The generator
// ---------------------------------------------------------------------------

class ibAstGen
{
public:
	explicit ibAstGen(unsigned int seed) : m_state(seed ? seed : 1u) {}

	// A small LCG (Numerical Recipes constants) — reproducible everywhere, no <random> engine
	// differences between toolchains to explain away.
	//
	// ⚠⚠ AND ITS OUTPUT IS MIXED BEFORE ANYONE TAKES A REMAINDER. An LCG's LOW bits barely vary —
	// bit 0 alternates, bit 1 has period 4 — and `% 2` / `% 4` read exactly those. Returned raw, the
	// state made `Chance(4)` fire on a schedule rather than a spread, and `TOTALS` was never
	// generated ONCE across 400 seeds. The property test passed on all of them, which is the whole
	// danger: a generator that quietly produces less than it claims makes a green run mean less than
	// it looks. TheGeneratorReallyProducesTheAwkwardShapes below is what caught it.
	unsigned int Next()
	{
		m_state = m_state * 1664525u + 1013904223u;
		unsigned int x = m_state;         // finalizer — spread the high entropy down into bit 0
		x ^= x >> 16;
		x *= 2246822519u;
		x ^= x >> 13;
		return x;
	}
	unsigned int Below(unsigned int n) { return n ? Next() % n : 0u; }
	bool Chance(unsigned int oneIn) { return Below(oneIn) == 0u; }

	// ⚠ TWO POOLS, and the line between them is a LANGUAGE DECISION, not a convenience.
	//
	// `SafeName` is what may stand anywhere. `AnyName` also draws words this language RESERVES —
	// `Order`, `Group`, `Value`, `Count`, `Index`, `Update` — because those are entirely ordinary
	// things to call a document or an attribute, and a configuration names them without consulting
	// our keyword table. That is the defect that started this file.
	//
	// Where a reserved word is allowed:
	//   * AFTER A DOT — always. Nothing but a name can follow one, so there is nothing to resolve.
	//   * AS AN ALIAS — never, and deliberately: `… AS Inner` reads as the start of a join to a
	//     person as much as to the parser. Settled earlier, by a test of Max's that was right and a
	//     test of mine that was wrong.
	//   * AS THE FIRST SEGMENT of an unqualified path — NOT today. `SELECT Order` would have to be
	//     told apart from every clause that starts with a keyword, and the qualified form
	//     (`Products.Order`) already works — which is what the constructor writes, always. Recorded
	//     as a known limit rather than smuggled in under a generator run.
	wxString SafeName()
	{
		static const wxChar* s_names[] = {
			wxT("Code"), wxT("Description"), wxT("Parent"), wxT("Owner"), wxT("Qty"), wxT("Price"),
			wxT("Reference"), wxT("Number"), wxT("Period"), wxT("Warehouse")
		};
		return s_names[Below(static_cast<unsigned int>(std::size(s_names)))];
	}

	wxString AnyName()
	{
		static const wxChar* s_reserved[] = {
			wxT("Order"), wxT("Group"), wxT("Value"), wxT("Count"), wxT("Index"), wxT("Update"),
			wxT("Elements"), wxT("Date")
		};
		// ⚠ BOTH BRANCHES ARE wxString. A ternary mixing `wxString` with a `const wxChar*` compiles
		// on MSVC and is AMBIGUOUS to Clang and ill-typed to GCC — each can convert either way.
		return Chance(2) ? SafeName()
		                 : wxString(s_reserved[Below(static_cast<unsigned int>(std::size(s_reserved)))]);
	}

	wxString SourceName()
	{
		static const wxChar* s_kinds[] = { wxT("Catalog"), wxT("Document"), wxT("InformationRegister") };
		return wxString(s_kinds[Below(3)]) + wxT(".") + AnyName();   // the metaobject may be `Document.Order`
	}

	// A dotted column path: one to three segments. The FIRST is safe (see the two pools above); the
	// rest are drawn from everything, which is exactly the case that was broken.
	ibQueryAstExprPtr Column(unsigned int maxSegments = 3)
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		const unsigned int n = 1 + Below(maxSegments);
		for (unsigned int i = 0; i < n; ++i)
			e->m_path.push_back(i == 0 ? SafeName() : AnyName());
		return e;
	}

	ibQueryAstExprPtr Param()
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
		e->m_paramName = SafeName();
		return e;
	}

	ibQueryAstExprPtr Literal()
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		switch (Below(3)) {
		case 0:  e->m_literal = ibValue(ibNumber(static_cast<long>(Below(1000)))); break;
		case 1:  e->m_literal = ibValue(SafeName()); break;
		default: e->m_literal = ibValue(Below(2) != 0u); break;
		}
		return e;
	}

	ibQueryAstExprPtr Cast()
	{
		auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
		cast->m_arg = Column(1);
		cast->m_path.push_back(wxT("Document"));
		cast->m_path.push_back(AnyName());
		if (Chance(2))
			return cast;

		// …and the walk THROUGH it, which is a Column rooted on the cast. The shape that never
		// worked, so it is generated deliberately rather than left to chance.
		auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		column->m_arg = cast;
		const unsigned int n = 1 + Below(2);
		for (unsigned int i = 0; i < n; ++i)
			column->m_path.push_back(AnyName());
		return column;
	}

	ibQueryAstExprPtr Aggregate()
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Func);
		static const ibQueryKeyword s_fns[] = { ibQueryKeyword::Sum, ibQueryKeyword::Count,
		                                        ibQueryKeyword::Min, ibQueryKeyword::Max,
		                                        ibQueryKeyword::Avg };
		e->m_func = s_fns[Below(5)];
		if (e->m_func == ibQueryKeyword::Count && Chance(4)) {
			e->m_star = true;
			return e;
		}
		e->m_distinctArg = ibDistinctMattersFor(e->m_func) && Chance(3);
		e->m_arg = Chance(4) ? Arith(0) : Column(2);
		return e;
	}

	// Arithmetic, which is where the composite aggregate lives — `SUM(a) / COUNT(b) * 1.2` is the
	// shape the projection reader used to give up on.
	ibQueryAstExprPtr Arith(unsigned int depth)
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		static const ibQueryArithOp s_ops[] = { ibQueryArithOp::Add, ibQueryArithOp::Sub,
		                                        ibQueryArithOp::Mul, ibQueryArithOp::Div,
		                                        ibQueryArithOp::Mod };
		e->m_arith = s_ops[Below(5)];
		e->m_lhs = Term(depth + 1);
		e->m_rhs = Term(depth + 1);
		return e;
	}

	// A value-yielding expression — what a projection or an arithmetic operand may be.
	ibQueryAstExprPtr Term(unsigned int depth)
	{
		if (depth >= 2)
			return Chance(2) ? Column(2) : Literal();
		switch (Below(7)) {
		case 0:  return Literal();
		case 1:  return Param();
		case 2:  return Aggregate();
		case 3:  return Arith(depth);
		case 4:  return Cast();
		case 5:  return CaseWhen(depth);
		default: return Column(3);
		}
	}

	ibQueryAstExprPtr CaseWhen(unsigned int depth)
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Case);
		const unsigned int n = 1 + Below(2);
		for (unsigned int i = 0; i < n; ++i)
			e->m_cases.emplace_back(Predicate(depth + 1), Term(depth + 1));
		if (Chance(2))
			e->m_else = Term(depth + 1);
		return e;
	}

	ibQueryAstExprPtr Predicate(unsigned int depth)
	{
		if (depth >= 2)
			return Comparison(depth);
		switch (Below(7)) {
		case 0: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
			e->m_isOr = Chance(2);
			e->m_lhs  = Predicate(depth + 1);
			e->m_rhs  = Predicate(depth + 1);
			return e;
		}
		case 1: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
			e->m_lhs = Predicate(depth + 1);
			return e;
		}
		case 2: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::IsNull);
			e->m_negated = Chance(2);
			e->m_lhs = Column(2);
			return e;
		}
		case 3: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Between);
			e->m_negated = Chance(3);
			e->m_lhs  = Column(2);
			e->m_low  = Literal();
			e->m_high = Literal();
			return e;
		}
		case 4: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::In);
			e->m_negated = Chance(3);
			e->m_lhs = Column(2);
			const unsigned int n = 1 + Below(3);
			for (unsigned int i = 0; i < n; ++i)
				e->m_list.push_back(Literal());
			return e;
		}
		case 5: {
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Like);
			e->m_negated = Chance(3);
			e->m_lhs = Column(2);
			e->m_rhs = Literal();
			return e;
		}
		default: return Comparison(depth);
		}
	}

	ibQueryAstExprPtr Comparison(unsigned int depth)
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Compare);
		static const ibQueryCompareOp s_ops[] = { ibQueryCompareOp::Eq, ibQueryCompareOp::Ne,
		                                          ibQueryCompareOp::Lt, ibQueryCompareOp::Le,
		                                          ibQueryCompareOp::Gt, ibQueryCompareOp::Ge };
		e->m_cmp = s_ops[Below(6)];
		e->m_lhs = Term(depth + 1);
		e->m_rhs = Chance(2) ? Literal() : Param();
		return e;
	}

	ibQuerySource Source(unsigned int depth, bool allowSubquery)
	{
		ibQuerySource s;
		if (allowSubquery && depth < 1 && Chance(5)) {
			s.m_subquery = Select(depth + 1, /*allowTail*/false);
			s.m_alias = SafeName();  // a nested source without a name is legal but pointless here
			return s;
		}
		const wxString dotted = SourceName();
		s.m_name.push_back(dotted.BeforeFirst(wxT('.')));
		s.m_name.push_back(dotted.AfterFirst(wxT('.')));
		if (Chance(2))
			s.m_alias = SafeName();
		return s;
	}

	ibQuerySelectPtr Select(unsigned int depth, bool allowTail)
	{
		auto s = std::make_shared<ibQuerySelect>();

		s->m_distinct = Chance(6);
		s->m_allowed  = Chance(8);
		if (Chance(6))
			s->m_top = static_cast<long>(1 + Below(100));

		const unsigned int projections = 1 + Below(3);
		for (unsigned int i = 0; i < projections; ++i) {
			ibQueryProjection p;
			p.m_expr = Term(depth);
			// An alias is written on purpose about half the time — a name the reader must give back
			// unchanged, including when it collides with the natural one.
			if (Chance(2))
				p.m_alias = SafeName() + wxString::Format(wxT("%u"), i);
			s->m_projections.push_back(p);
		}

		s->m_from = Source(depth, /*allowSubquery*/true);

		const unsigned int joins = Below(3);
		for (unsigned int j = 0; j < joins; ++j) {
			ibQueryAstJoin join;
			static const ibQueryJoinKindAst s_kinds[] = { ibQueryJoinKindAst::Inner, ibQueryJoinKindAst::Left,
			                                             ibQueryJoinKindAst::Right, ibQueryJoinKindAst::Full };
			join.m_kind   = s_kinds[Below(4)];
			join.m_source = Source(depth + 1, /*allowSubquery*/false);
			join.m_on     = Comparison(1);
			s->m_joins.push_back(join);
		}

		if (Chance(2))
			s->m_where = Predicate(0);

		if (Chance(3)) {
			const unsigned int keys = 1 + Below(2);
			for (unsigned int k = 0; k < keys; ++k)
				s->m_groupBy.push_back(Column(2));
			if (Chance(2)) {
				auto having = ibQueryAstExpr::Make(ibQueryAstExprKind::Compare);
				having->m_cmp = ibQueryCompareOp::Gt;
				having->m_lhs = Aggregate();
				having->m_rhs = Literal();
				s->m_having = having;
			}
		}

		if (!allowTail)
			return s;

		if (Chance(4)) {
			const unsigned int n = 1 + Below(2);
			for (unsigned int i = 0; i < n; ++i) {
				ibQueryOrderItem item;
				item.m_expr = Column(2);
				item.m_ascending = Chance(2);
				s->m_orderBy.push_back(item);
			}
		}

		// TOTALS, including the shapes that only became legal today: OVERALL on its own, and
		// OVERALL above a dimension list.
		if (Chance(4)) {
			s->m_hasTotals = true;
			s->m_totalsOverall = Chance(2);
			const unsigned int aggs = Below(3);
			for (unsigned int i = 0; i < aggs; ++i)
			{
				ibQueryTotalAggregate resource;
				resource.m_expr = SimpleAggregate();
				// …AND SOMETIMES A NAME. `TOTALS SUM(x) AS Qty` is exactly the kind of word a round
				// trip drops: the figure still comes back, under a name nobody asked for.
				if (Chance(3))
					resource.m_alias = wxString::Format(wxT("Res%u"), i);
				s->m_totalsAggregates.push_back(std::move(resource));
			}
			const unsigned int dims = s->m_totalsOverall ? Below(3) : 1 + Below(2);
			for (unsigned int d = 0; d < dims; ++d) {
				ibQueryTotalDim dim;
				// A LEVEL OF SEVERAL FIELDS is generated as often as a plain one, because the bracket
				// that holds it together is exactly the spelling a round trip can lose: read back
				// without it, one level of two fields becomes two levels of one and the report changes
				// shape without a word.
				const unsigned int fields = Chance(3) ? 2u : 1u;
				for (unsigned int f = 0; f < fields; ++f) {
					ibQueryTotalField field;
					field.m_expr = Column(2);
					// Only a single-field level may unfold through a hierarchy — the same rule the
					// engine holds, so the generator does not manufacture queries it refuses.
					if (fields == 1) {
						switch (Below(3)) {
						case 0:  field.m_unfold = ibQueryDimUnfold::Hierarchy; break;
						case 1:  field.m_unfold = ibQueryDimUnfold::HierarchyOnly; break;
						default: field.m_unfold = ibQueryDimUnfold::Elements; break;
						}
					}
					dim.m_fields.push_back(std::move(field));
				}
				if (Chance(3))
					dim.m_alias = SafeName() + wxString::Format(wxT("L%u"), d);
				s->m_totalsBy.push_back(dim);
			}
		}

		return s;
	}

	// The TOTALS aggregate list is a list of CALLS — the terminal folds each into its own column,
	// so a composite measure has no home there and the parser says so. Generating one would be
	// generating a query the language deliberately refuses.
	ibQueryAstExprPtr SimpleAggregate()
	{
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Func);
		static const ibQueryKeyword s_fns[] = { ibQueryKeyword::Sum, ibQueryKeyword::Count,
		                                        ibQueryKeyword::Min, ibQueryKeyword::Max,
		                                        ibQueryKeyword::Avg };
		e->m_func = s_fns[Below(5)];
		if (e->m_func == ibQueryKeyword::Count && Chance(4)) {
			e->m_star = true;
			return e;
		}
		e->m_distinctArg = ibDistinctMattersFor(e->m_func) && Chance(3);
		e->m_arg = Column(2);
		return e;
	}

private:
	unsigned int m_state;
};

// ---------------------------------------------------------------------------
//  The property
// ---------------------------------------------------------------------------

// render → parse → render, and the two texts must agree. Returns the failure story, empty on
// success, so the caller can name the seed AND show the query.
wxString RoundTripFailure(const ibQuerySelect& ast)
{
	wxString written;
	try {
		written = ibRenderQuery(ast);
	}
	catch (const ibBackendException& e) {
		return wxT("RENDER RAISED: ") + e.GetErrorDescription();
	}

	ibQuerySelectPtr read;
	try {
		ibQueryParser parser;
		read = parser.Parse(written);
	}
	catch (const ibBackendException& e) {
		return wxT("PARSE REFUSED WHAT WE WROTE:\n") + written + wxT("\n  -> ") + e.GetErrorDescription();
	}
	if (!read)
		return wxT("PARSE RETURNED NOTHING FOR:\n") + written;

	wxString again;
	try {
		again = ibRenderQuery(*read);
	}
	catch (const ibBackendException& e) {
		return wxT("SECOND RENDER RAISED: ") + e.GetErrorDescription() + wxT("\nfor:\n") + written;
	}

	if (again != written)
		return wxT("THE TRIP CHANGED THE QUERY.\nfirst:\n") + written + wxT("\nsecond:\n") + again;

	return wxEmptyString;
}

} // namespace

// ===========================================================================

TEST(QueryRoundTripProperty, GeneratedQueriesSurviveTheTrip)
{
	// The seeds are fixed, so a failure here is reproducible by anyone and bisectable. Widening the
	// count is free; widening the GENERATOR is what actually finds more.
	int checked = 0;
	for (unsigned int seed = 1; seed <= 400; ++seed) {
		ibAstGen gen(seed * 2654435761u);
		const ibQuerySelectPtr ast = gen.Select(0, /*allowTail*/true);
		ASSERT_NE(nullptr, ast);

		const wxString failure = RoundTripFailure(*ast);
		EXPECT_TRUE(failure.IsEmpty()) << "seed " << seed << "\n" << failure.ToStdString();
		++checked;
	}
	EXPECT_EQ(400, checked);
}

TEST(QueryRoundTripProperty, TheGeneratorReallyProducesTheAwkwardShapes)
{
	// ⚠ A PROPERTY TEST THAT GENERATES NOTHING INTERESTING PASSES FOR THE WRONG REASON. This is the
	// generator's own test: over the same seeds, the shapes that actually broke must ALL occur —
	// a keyword used as a name, a walk after a cast, arithmetic over aggregates, and OVERALL.
	bool keywordName = false, castWalk = false, aggArith = false, overall = false, multiFieldLevel = false;

	for (unsigned int seed = 1; seed <= 400; ++seed) {
		ibAstGen gen(seed * 2654435761u);
		const ibQuerySelectPtr ast = gen.Select(0, true);
		const wxString text = ibRenderQuery(*ast);

		for (const ibQueryTotalDim& dim : ast->m_totalsBy)
			if (dim.m_fields.size() > 1)
				multiFieldLevel = true;

		if (text.Contains(wxT(".Order")) || text.Contains(wxT(".Group"))
		    || text.Contains(wxT(".Count")) || text.Contains(wxT(".Value"))
		    || text.Contains(wxT(".Index")) || text.Contains(wxT(".Update")))
			keywordName = true;
		if (text.Contains(wxT(").")))
			castWalk = true;
		if (text.Contains(wxT(") /")) || text.Contains(wxT(") *")) || text.Contains(wxT(") +")))
			aggArith = true;
		if (text.Contains(wxT("OVERALL")))
			overall = true;
	}

	EXPECT_TRUE(keywordName) << "no metaobject was ever named with a reserved word";
	EXPECT_TRUE(castWalk)    << "no walk after a cast was ever generated";
	EXPECT_TRUE(aggArith)    << "no arithmetic over a call was ever generated";
	EXPECT_TRUE(overall)     << "BY OVERALL never occurred";
	EXPECT_TRUE(multiFieldLevel) << "no TOTALS level of several fields was ever generated";
}

// ⭐ WINDOWS MAKE THE TRIP TOO — and they are checked by hand because the generator does not build
// them: it walks the AST shapes it knows, and `m_over` arrived later. A window is exactly the kind
// of thing the standing asymmetry above bites — the renderer emits it on every constructor refresh,
// while the parser only meets it in text somebody wrote.
TEST(QueryRoundTripProperty, WindowsSurviveTheTrip)
{
	const wxChar* queries[] = {
		wxT("SELECT SUM(Amount) OVER (PARTITION BY Region) FROM Document.Sales"),
		wxT("SELECT SUM(Amount) OVER (PARTITION BY Region ORDER BY Period RANGE) FROM Document.Sales"),
		wxT("SELECT SUM(Amount) OVER (ORDER BY Period DESC ROWS) FROM Document.Sales"),
		wxT("SELECT ROW_NUMBER() OVER (PARTITION BY Region ORDER BY Amount DESC) FROM Document.Sales"),
		wxT("SELECT DENSE_RANK() OVER (ORDER BY Amount) FROM Document.Sales"),
	};

	for (const wxChar* text : queries) {
		ibQueryParser parser;
		ibQuerySelectPtr ast;
		ASSERT_NO_THROW(ast = parser.Parse(text)) << text;
		ASSERT_NE(nullptr, ast) << text;

		const wxString failure = RoundTripFailure(*ast);
		EXPECT_TRUE(failure.IsEmpty()) << text << "\n" << failure.ToStdString();
	}
}
