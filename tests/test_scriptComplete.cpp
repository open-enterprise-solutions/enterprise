// =============================================================================
// OES Enterprise — what the editor is told at a caret (compiler/scriptComplete)
//
// The three doors read ONE artefact — the bytecode the compiler just produced —
// and until this file there was no test of any of them. The battery that drove
// the work was a live one (a running designer answering over MCP), which is the
// right instrument for measuring coverage over real modules and the wrong one
// for CI: it needs an application, a configuration and a database.
//
// These need none of the three. `moduleObject` is passed rather than reached
// for, and null answers about the bare language (scriptComplete.h) — so a text,
// a caret and an expectation is the whole fixture.
//
// ⚠ THE BINARY IS FORCED TO VES for the whole run (test_compiler.cpp's global
// environment), so the sources here are written Function…EndFunction.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/scriptComplete.h"

namespace {

// The caret is written INTO the source as `$` and removed before compiling. A
// position counted by hand is a number that drifts the moment a line above it
// is edited, and then the test measures a different caret than it reads as.
std::vector<ibCaretName> NamesAt(const wxString& marked)
{
	const size_t at = marked.find(wxT('$'));
	EXPECT_NE(at, wxString::npos) << "the source carries no $ caret marker";

	wxString text = marked;
	text.Remove(at, 1);

	std::vector<ibCaretName> names;
	ibNamesAtCaret(text, (unsigned int)at, nullptr, names);
	return names;
}

bool Offers(const std::vector<ibCaretName>& names, const wxString& name)
{
	for (const ibCaretName& entry : names)
		if (entry.m_name.IsSameAs(name, false))
			return true;
	return false;
}

// Names its failure: a bare EXPECT_FALSE(Offers(...)) prints "Actual: true" and
// leaves the list — the one thing worth reading — out of the report.
::testing::AssertionResult DoesNotOffer(const std::vector<ibCaretName>& names, const wxString& name)
{
	if (!Offers(names, name))
		return ::testing::AssertionSuccess();

	wxString all;
	for (const ibCaretName& entry : names)
		all << entry.m_name << wxT(" ");
	return ::testing::AssertionFailure()
		<< "offered `" << name.ToStdString() << "`; the list was: " << all.ToStdString();
}

} // namespace

// ===========================================================================
// The order gate — a name written below the caret cannot be written here
// ===========================================================================

// ⭐⭐ THE DEFECT THIS FILE WAS OPENED FOR. `a = X;` with no `var` is an implicit
// local, and the compiler writes NO tape declarator for one — so the gate had no
// position to compare and the name was offered at every caret in the body,
// twelve lines above the statement that creates it (Max, screenshot 2026-09-09).
TEST(NamesAtCaret, AnImplicitVariableIsNotOfferedAboveItsOwnCreation)
{
	const std::vector<ibCaretName> names = NamesAt(
		wxT("Function Handler() Public\n")
		wxT("  var records;\n")
		wxT("  records = 1;\n")
		wxT("  $\n")
		wxT("  a = 2;\n")
		wxT("  Return records;\n")
		wxT("EndFunction\n"));

	EXPECT_TRUE(DoesNotOffer(names, wxT("a")));
	// The control: a name written ABOVE is exactly as visible as it ever was, so
	// a green first line cannot mean "the list came back empty".
	EXPECT_TRUE(Offers(names, wxT("records")));
}

// The other half of the same rule, and the one that says the gate did not simply
// become "hide every implicit variable": below its creation it is an ordinary
// name.
TEST(NamesAtCaret, AnImplicitVariableIsOfferedBelowItsCreation)
{
	const std::vector<ibCaretName> names = NamesAt(
		wxT("Function Handler() Public\n")
		wxT("  total = 1;\n")
		wxT("  $\n")
		wxT("  Return total;\n")
		wxT("EndFunction\n"));

	EXPECT_TRUE(Offers(names, wxT("total")));
}

// A `var` carries its declarator's position on the tape, so this half of the
// gate predates the implicit-variable work — pinned here because both halves now
// answer through the same lambda and a change to one can silently move the other.
TEST(NamesAtCaret, ADeclaredVariableIsNotOfferedAboveItsDeclaration)
{
	const std::vector<ibCaretName> names = NamesAt(
		wxT("Function Handler() Public\n")
		wxT("  $\n")
		wxT("  var later;\n")
		wxT("  Return 1;\n")
		wxT("EndFunction\n"));

	EXPECT_TRUE(DoesNotOffer(names, wxT("later")));
}

// 🛑 A PARAMETER IS EXEMPT FROM THE ORDER QUESTION — it is visible throughout its
// body by the language's own rule. It reaches the gate at all only because the
// frame's locals table holds the parameters too, and it has no declarator
// either, so without the exemption it would be dated by its FIRST USE and hidden
// from every caret above that.
TEST(NamesAtCaret, AParameterIsOfferedAboveItsFirstUse)
{
	const std::vector<ibCaretName> names = NamesAt(
		wxT("Function Handler(Amount) Public\n")
		wxT("  $\n")
		wxT("  Return Amount;\n")
		wxT("EndFunction\n"));

	EXPECT_TRUE(Offers(names, wxT("Amount")));
}

// A name is not a candidate for its own completion: typing `cat` compiles as a
// name this text declares, and the dropdown listed `cat` above `Catalogs`.
TEST(NamesAtCaret, TheWordBeingTypedIsNotOfferedAsItsOwnCompletion)
{
	const std::vector<ibCaretName> names = NamesAt(
		wxT("Function Handler() Public\n")
		wxT("  cat$\n")
		wxT("EndFunction\n"));

	EXPECT_TRUE(DoesNotOffer(names, wxT("cat")));
}

// ===========================================================================
// The query outline — what the compiler understood about a query it read
// ===========================================================================

TEST(ScriptQueryOutline, AQueryReportsItsBindingAndItsColumns)
{
	const std::vector<ibQueryOutline> queries = ibOutlineScriptQueries(
		wxT("Function Handler() Public\n")
		wxT("  var rows; var q;\n")
		wxT("  rows = New Array;\n")
		wxT("  q = from r in rows select { A = r.A };\n")
		wxT("  Return q;\n")
		wxT("EndFunction\n"));

	ASSERT_EQ(queries.size(), 1u);
	EXPECT_FALSE(queries[0].m_isRestrict);
	ASSERT_EQ(queries[0].m_bindings.size(), 1u);
	EXPECT_EQ(queries[0].m_bindings[0].m_name, wxT("r"));
	EXPECT_EQ(queries[0].m_bindings[0].m_origin, wxT("from"));
	ASSERT_EQ(queries[0].m_columns.size(), 1u);
	EXPECT_EQ(queries[0].m_columns[0], wxT("A"));
}

// Several ordering keys are still ONE ordering, so the outline says "it orders"
// once — the shape a reader asks about, not the number of KEEP instructions
// underneath it.
TEST(ScriptQueryOutline, AMultiKeyOrderByIsReportedAsAnOrdering)
{
	const std::vector<ibQueryOutline> queries = ibOutlineScriptQueries(
		wxT("Function Handler() Public\n")
		wxT("  var rows; var q;\n")
		wxT("  rows = New Array;\n")
		wxT("  q = from r in rows orderby r.A, r.B descending select { A = r.A };\n")
		wxT("  Return q;\n")
		wxT("EndFunction\n"));

	ASSERT_EQ(queries.size(), 1u);
	EXPECT_TRUE(queries[0].m_orders);
	EXPECT_TRUE(queries[0].m_orderDescending);
}

// The text a query was read from is HANDED BACK rather than regenerated — the
// two positions bracket the stretch the compiler actually read, and that is what
// makes editing a query and re-reading its shape one loop instead of two
// representations to keep in step.
TEST(ScriptQueryOutline, AQueryCarriesTheTextItWasReadFrom)
{
	const std::vector<ibQueryOutline> queries = ibOutlineScriptQueries(
		wxT("Function Handler() Public\n")
		wxT("  var rows; var q;\n")
		wxT("  rows = New Array;\n")
		wxT("  q = from r in rows where r.A > 1 select { A = r.A };\n")
		wxT("  Return q;\n")
		wxT("EndFunction\n"));

	ASSERT_EQ(queries.size(), 1u);
	EXPECT_LT(queries[0].m_textFrom, queries[0].m_textTo);
	EXPECT_TRUE(queries[0].m_text.Contains(wxT("from r in rows")));
}
