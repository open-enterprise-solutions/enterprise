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
		// No document: the literal scan reads the DOCUMENT TEXT and the caret, and nothing else —
		// which is exactly why it can be tested without a module behind it.
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

	// `""` inside a string is an escaped quote, not a close followed by an open — which is why the
	// scan starts at the top of the document instead of looking around the caret.
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
