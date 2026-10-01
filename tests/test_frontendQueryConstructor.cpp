// =============================================================================
// The query constructor — the two pieces that live in the GUI half and can be
// wrong silently.
//
//   * THE STRING LITERAL UNDER THE CARET. A query written in a module lives in
//     a literal, usually multi-line: quoted, inner quotes doubled, continuation
//     lines opened with `|`. Getting the SPAN or the unescaping wrong does not
//     crash — it hands the parser a query with `|` characters in it, or writes
//     back over the wrong stretch of somebody's module. Both are quiet, and both
//     are what these tests are here to stop.
//
//   * READ-ONLY. The constructor asks the METADATA whether the configuration may
//     be changed and can only ever tighten that answer, never loosen it.
//
// The dialog's tab wiring is not tested here: it is a view over an AST whose
// every clause is already pinned by the round-trip tests (test_queryL4Parser),
// and its source/field answers by test_queryConstructor.
// =============================================================================

#include "frontendFix.h"

#include "frontend/win/editor/codeEditor/codeEditor.h"
#include "frontend/win/dlgs/queryConstructor/queryConstructor.h"
#include "frontend/win/dlgs/queryConstructor/queryCaseDialog.h"
#include "frontend/win/dlgs/queryConstructor/querySelectionLinkModel.h"   // ibQueryResectionLinks
#include "backend/query/queryRender.h"
#include "backend/query/queryParser.h"

#include <wx/frame.h>

namespace {

// A bare editor on a frame of its own — enough for the caret and the document,
// which is all the literal scan reads.
class CodeEditorFix : public FrontendRuntimeFix
{
protected:
	void SetUp() override
	{
		FrontendRuntimeFix::SetUp();     // GTEST_SKIPs on a headless box
		if (!ready) return;
		m_frame = new wxFrame(nullptr, wxID_ANY, wxT("literal"));
		// No document: the literal is asked of the editor's own token stream and the caret, and a
		// document-less editor re-lexes on every change — so SetText is all the setup it needs.
		m_editor = new ibCodeEditor(nullptr, m_frame, wxID_ANY);
		m_ready = m_editor != nullptr;
	}

	void TearDown() override
	{
		if (m_frame != nullptr) { m_frame->Destroy(); m_frame = nullptr; m_editor = nullptr; }
		FrontendRuntimeFix::TearDown();
	}

	// Put `text` in the editor and the caret on the first `|` marker position given by `caret`.
	ibCodeEditor::StringLiteralSpan SpanAt(const wxString& text, int caret)
	{
		m_editor->SetText(text);
		m_editor->SetCurrentPos(caret);
		return m_editor->GetStringLiteralUnderCursor();
	}

	wxFrame*      m_frame  = nullptr;
	ibCodeEditor* m_editor = nullptr;
	bool          m_ready  = false;
};

} // namespace

TEST_F(CodeEditorFix, NoLiteralUnderTheCaretIsNotFound)
{
	if (!m_ready) GTEST_SKIP();

	const ibCodeEditor::StringLiteralSpan span = SpanAt(wxT("x = 1;"), 2);
	EXPECT_FALSE(span.Found());
}

TEST_F(CodeEditorFix, ASingleLineLiteralIsFoundAndUnquoted)
{
	if (!m_ready) GTEST_SKIP();

	const wxString source = wxT("q = \"SELECT Ref FROM Catalog.Products\";");
	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, 10);

	ASSERT_TRUE(span.Found());
	EXPECT_EQ(wxT("SELECT Ref FROM Catalog.Products"), span.m_text);
	EXPECT_EQ(wxT('"'), source[span.m_start]);
	EXPECT_EQ(wxT('"'), source[span.m_end - 1]);
}

TEST_F(CodeEditorFix, TheCaretCountsAtEitherEdgeOfTheLiteral)
{
	if (!m_ready) GTEST_SKIP();

	// A click on the opening or the closing quote is plainly pointing at that string; a scan that
	// only accepted the inside would make the menu item grey exactly where a person aimed.
	const wxString source = wxT("q = \"abc\";");
	EXPECT_TRUE(SpanAt(source, 4).Found());    // on the opening quote
	EXPECT_TRUE(SpanAt(source, 9).Found());    // just past the closing quote
}

TEST_F(CodeEditorFix, AMultiLineLiteralLosesItsContinuationMarkers)
{
	if (!m_ready) GTEST_SKIP();

	// THE CASE THAT MATTERS: the parser must see the query language, not the script's spelling
	// of it. A `|` left in the text is a lex error the author never wrote.
	const wxString source =
		wxT("q = \"SELECT Ref\n")
		wxT("     |FROM Catalog.Products\n")
		wxT("     |WHERE Code = 1\";");

	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, 12);
	ASSERT_TRUE(span.Found());
	EXPECT_EQ(wxT("SELECT Ref\nFROM Catalog.Products\nWHERE Code = 1"), span.m_text);

	// And the engine reads what came out — which is the whole point of stripping it.
	ibQueryParser parser;
	EXPECT_NO_THROW((void)parser.ParsePackage(span.m_text));
}

TEST_F(CodeEditorFix, DoubledQuotesComeBackAsOne)
{
	if (!m_ready) GTEST_SKIP();

	const ibCodeEditor::StringLiteralSpan span =
		SpanAt(wxT("q = \"WHERE Code = \"\"A\"\"\";"), 10);
	ASSERT_TRUE(span.Found());
	EXPECT_EQ(wxT("WHERE Code = \"A\""), span.m_text);
}

TEST_F(CodeEditorFix, TheSecondLiteralOnALineIsTheOneTheCaretIsIn)
{
	if (!m_ready) GTEST_SKIP();

	// Two literals on one line are two tokens; the caret picks the one it stands in.
	const wxString source = wxT("f(\"one\", \"two\");");
	const ibCodeEditor::StringLiteralSpan first  = SpanAt(source, 4);
	const ibCodeEditor::StringLiteralSpan second = SpanAt(source, 11);

	ASSERT_TRUE(first.Found());
	ASSERT_TRUE(second.Found());
	EXPECT_EQ(wxT("one"), first.m_text);
	EXPECT_EQ(wxT("two"), second.m_text);
}

TEST_F(CodeEditorFix, WritingBackReplacesExactlyThatLiteral)
{
	if (!m_ready) GTEST_SKIP();

	const wxString source = wxT("before(); q = \"old\"; after();");
	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, 16);
	ASSERT_TRUE(span.Found());

	m_editor->ReplaceStringLiteral(span, wxT("SELECT Ref\nFROM Catalog.Products"));
	const wxString written = m_editor->GetText();

	EXPECT_TRUE(written.StartsWith(wxT("before(); q = \"SELECT Ref\n")))
		<< written.ToStdString();
	EXPECT_TRUE(written.EndsWith(wxT("; after();"))) << written.ToStdString();
	EXPECT_TRUE(written.Contains(wxT("|FROM Catalog.Products\"")))
		<< "a continuation line must be reopened with '|'\n" << written.ToStdString();

	// The round trip closes: what was written reads back as what went in.
	const ibCodeEditor::StringLiteralSpan again = SpanAt(written, 16);
	ASSERT_TRUE(again.Found());
	EXPECT_EQ(wxT("SELECT Ref\nFROM Catalog.Products"), again.m_text);
}

// ⭐⭐ THE CARET IS A wxSTC POSITION — A BYTE OFFSET INTO UTF-8 — and the literal the constructor
// opens must be found and written back in that same measure. The scan this replaced counted
// characters of GetText() against it: every Cyrillic letter above a query moved the caret one byte
// further on than the scan believed, so a click near the end of the query opened the NEXT literal,
// and OK wrote the query that many bytes too early, over the code in front of it.
TEST_F(CodeEditorFix, TextInRussianAboveTheQueryDoesNotMoveTheLiteral)
{
	if (!m_ready) GTEST_SKIP();

	const wxString source =
		wxT("Message(\"\\u041D\\u0435 \\u0443\\u043A\\u0430\\u0437\\u0430\\u043D \\u043C\\u0435\\u0441\\u044F\\u0446\");\n")
		wxT("q = \"SELECT Ref FROM Catalog.Products\"; p = \"MonthEnd\";");
	// The caret on the last letter of the query - where a byte-for-character slip lands in "MonthEnd".
	const int at = source.Find(wxT("Products\"")) + 7;
	const int caret = static_cast<int>(source.Left(at).ToUTF8().length());

	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, caret);
	ASSERT_TRUE(span.Found());
	EXPECT_EQ(wxT("SELECT Ref FROM Catalog.Products"), span.m_text);
	EXPECT_EQ('"', m_editor->GetCharAt(span.m_start));
	EXPECT_EQ('"', m_editor->GetCharAt(span.m_end - 1));

	// And written back exactly over itself: the message above and the literal after are untouched.
	m_editor->ReplaceStringLiteral(span, wxT("SELECT Code FROM Catalog.Products"));
	const wxString written = m_editor->GetText();
	EXPECT_TRUE(written.StartsWith(source.BeforeFirst(wxT('\n')) + wxT("\nq = \"SELECT Code FROM Catalog.Products\"; p = \"MonthEnd\";")))
		<< written.ToStdString(wxConvUTF8);
}

// A comment is not code: a quote in it opens nothing. The old scan counted it, and every literal
// below one odd quote in a `//` line was read inside out.
TEST_F(CodeEditorFix, AQuoteInACommentOpensNothing)
{
	if (!m_ready) GTEST_SKIP();

	const wxString source =
		wxT("// the \"odd one out\n")
		wxT("q = \"SELECT Ref FROM Catalog.Products\";");
	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, source.Find(wxT("Ref")));
	ASSERT_TRUE(span.Found());
	EXPECT_EQ(wxT("SELECT Ref FROM Catalog.Products"), span.m_text);
}

// A doubled quote is two bytes of spelling for one character of value. The lexer's UTF-8 count
// stepped over only one of them, so a literal holding `""` closed one byte early — the write-back
// left a stray quote behind, and every position after it in the module was one short.
TEST_F(CodeEditorFix, ALiteralWithDoubledQuotesIsWrittenBackWhole)
{
	if (!m_ready) GTEST_SKIP();

	const wxString source = wxT("q = \"WHERE Code = \"\"A\"\"\"; after();");
	const ibCodeEditor::StringLiteralSpan span = SpanAt(source, 10);
	ASSERT_TRUE(span.Found());
	EXPECT_EQ(source.Find(wxT("; after")), span.m_end);

	m_editor->ReplaceStringLiteral(span, wxT("SELECT Ref"));
	EXPECT_EQ(wxT("q = \"SELECT Ref\"; after();"), m_editor->GetText());

	// …and a literal AFTER it is still found where it is.
	const wxString two = wxT("a = \"x\"\"y\"; q = \"SELECT Ref\";");
	const ibCodeEditor::StringLiteralSpan second = SpanAt(two, two.Find(wxT("Ref")));
	ASSERT_TRUE(second.Found());
	EXPECT_EQ(wxT("SELECT Ref"), second.m_text);
}

// ----------------------------- read-only ------------------------------------

TEST(QueryConstructorReadOnly, NoConfigurationDoesNotMeanReadOnly)
{
	// A runtime host editing a dynamic list's own query has no metadata tree, and that must not
	// be mistaken for "you may not change this" — it is "there is no configuration in play".
	EXPECT_FALSE(ibDialogQueryConstructor::IsMetaDataReadOnly(nullptr));
}

// ----------------------- the CASE builder's round trip -----------------------
//
// `CASE WHEN … THEN … ELSE … END` is the one construction in this language that is an ORDERED LIST,
// which is why it has a window of its own (queryCaseDialog.h). The window owns the SHAPE — which
// branch, in what order — and no syntax: every cell is read by the engine's own parser.
//
// What can be wrong silently is the trip: a CASE opened here and handed back must be the SAME case.
// Losing a branch, dropping the ELSE, or reversing the order are all changes nobody would see until
// the query ran and answered differently. The dialog is built but never shown — the read and the
// write are the whole of what is being pinned.

namespace {

class QueryCaseFix : public FrontendRuntimeFix
{
protected:
	void SetUp() override
	{
		FrontendRuntimeFix::SetUp();     // GTEST_SKIPs on a headless box
		if (!ready) return;
		m_frame = new wxFrame(nullptr, wxID_ANY, wxT("case"));
		m_ready = true;
	}

	void TearDown() override
	{
		if (m_frame != nullptr) { m_frame->Destroy(); m_frame = nullptr; }
		FrontendRuntimeFix::TearDown();
	}

	// Parse `text` as an expression, open the builder on it, and render back what it hands over.
	wxString RoundTrip(const wxString& text) const
	{
		ibQueryParser parser;
		const ibQueryAstExprPtr existing = parser.ParseExpression(text);
		ibDialogQueryCase dialog(m_frame, std::vector<ibQueryConstructorField>(), existing);
		const ibQueryAstExprPtr built = dialog.GetExpression();
		return built ? ibRenderQueryExpr(*built) : wxString();
	}

	wxFrame* m_frame = nullptr;
	bool     m_ready = false;
};

} // namespace

TEST_F(QueryCaseFix, ACaseSurvivesTheTrip)
{
	if (!m_ready) GTEST_SKIP();
	const wxString text = wxT("CASE WHEN Qty > 10 THEN 1 ELSE 0 END");
	EXPECT_EQ(text, RoundTrip(text));
}

TEST_F(QueryCaseFix, EveryBranchComesBackAndInOrder)
{
	if (!m_ready) GTEST_SKIP();
	// ORDER IS THE MEANING: the first condition that holds decides the result, so a builder that
	// reordered the branches would change the answer without changing anything a reader can see.
	const wxString text = wxT("CASE WHEN Qty > 100 THEN 3 WHEN Qty > 10 THEN 2 WHEN Qty > 1 THEN 1 END");
	EXPECT_EQ(text, RoundTrip(text));
}

TEST_F(QueryCaseFix, NoElseIsNotTheSameAsElseNull)
{
	if (!m_ready) GTEST_SKIP();
	// The language distinguishes them and the window must not decide otherwise on the author's
	// behalf — an empty ELSE box means NO else branch.
	EXPECT_EQ(wxT("CASE WHEN Qty > 10 THEN 1 END"),
		RoundTrip(wxT("CASE WHEN Qty > 10 THEN 1 END")));
}

TEST_F(QueryCaseFix, SomethingThatIsNotACaseOpensEmptyAndBuildsNothing)
{
	if (!m_ready) GTEST_SKIP();
	// "Turn this into a choice" is a reasonable thing to mean, so a non-CASE is not refused — the
	// window opens empty. And an empty builder hands back NOTHING rather than `CASE END`, which the
	// parser refuses.
	EXPECT_TRUE(RoundTrip(wxT("Qty + 1")).IsEmpty());
}

// ===========================================================================
//  Links between named selections — kept in the shape the text is written in
// ===========================================================================

// ⭐ A LINK ADDED IN THE WINDOW IS WRITTEN — once it is complete enough to be. The row went into the
// package's list and no LINK statement stood for it, so the text never carried it and OK lost it.
TEST(QueryConstructorSelectionLinks, ALinkAddedInTheWindowIsWrittenAndReadBack)
{
	ibQueryParser parser;
	ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT Partner, Amount ONTO Sales FROM Document.Sales;")
		wxT("SELECT Partner, Amount ONTO Plan FROM Document.Plan"));
	ASSERT_EQ(package.m_statements.size(), 2u);

	// The row the window adds is empty: it stands for nothing yet, and writes nothing.
	std::vector<int> owners = ibQueryLinkOwners(package);
	package.m_links.push_back(ibQueryPackageLink());
	owners.push_back(-1);
	ibQueryResectionLinks(package, owners);
	EXPECT_EQ(package.m_statements.size(), 2u);
	EXPECT_EQ(package.m_links.size(), 1u) << "the row stays in the grid, waiting to be filled";
	EXPECT_FALSE(ibRenderQueryPackage(package).Contains(wxT("LINK")));

	// Filled in, it becomes a statement at the end of the package — and the text reads back.
	owners = ibQueryLinkOwners(package);
	package.m_links[0].m_left  = wxT("Sales");
	package.m_links[0].m_right = wxT("Plan");
	package.m_links[0].m_on    = parser.ParseExpression(wxT("Sales.Partner = Plan.Partner"));
	ibQueryResectionLinks(package, owners);
	ASSERT_EQ(package.m_statements.size(), 3u);
	EXPECT_TRUE(package.m_statements[2].IsLink());

	const wxString text = ibRenderQueryPackage(package);
	EXPECT_TRUE(text.Contains(wxT("LINK Sales"))) << text.ToStdString();
	const ibQueryPackage again = parser.ParsePackage(text);
	ASSERT_EQ(again.m_links.size(), 1u);
	EXPECT_EQ(again.m_links[0].m_right, wxT("Plan"));

	// Removed, it takes its statement with it — and the text has no LINK left.
	owners = ibQueryLinkOwners(package);
	package.m_links.erase(package.m_links.begin());
	owners.erase(owners.begin());
	ibQueryResectionLinks(package, owners);
	EXPECT_EQ(package.m_statements.size(), 2u);
	EXPECT_FALSE(ibRenderQueryPackage(package).Contains(wxT("LINK")));
}

// A second relation from the same selection joins its section, so the text says the head once.
TEST(QueryConstructorSelectionLinks, ASecondRelationOfOneHeadJoinsItsSection)
{
	ibQueryParser parser;
	ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT Partner ONTO Sales FROM Document.Sales;")
		wxT("SELECT Partner ONTO Plan FROM Document.Plan;")
		wxT("SELECT Partner ONTO Stock FROM Document.Stock;")
		wxT("LINK Sales JOIN Plan ON Sales.Partner = Plan.Partner"));
	ASSERT_EQ(package.m_statements.size(), 4u);

	std::vector<int> owners = ibQueryLinkOwners(package);
	ibQueryPackageLink second;
	second.m_left  = wxT("Sales");
	second.m_right = wxT("Stock");
	second.m_on    = parser.ParseExpression(wxT("Sales.Partner = Stock.Partner"));
	package.m_links.push_back(second);
	owners.push_back(-1);
	ibQueryResectionLinks(package, owners);

	EXPECT_EQ(package.m_statements.size(), 4u) << "no second LINK statement for the same head";
	const wxString text = ibRenderQueryPackage(package);
	const ibQueryPackage again = parser.ParsePackage(text);
	EXPECT_EQ(again.m_links.size(), 2u) << text.ToStdString();
	EXPECT_EQ(again.m_statements.size(), 4u);
}
