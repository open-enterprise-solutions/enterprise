// =============================================================================
// OES Enterprise — testing/* unit tests
//
// Covers:
//   * Discovery: `// @test` regex matches across CES/VES source variants.
//   * Filter matching: glob with `*` wildcard.
//   * Assertion outcomes: pass + fail + throws.
//   * Fixture push/pop balance.
//
// Full RunTests() is integration-level (requires activeMetaData); we
// exercise the building blocks here and lean on mcp-smoke.py for the
// end-to-end MCP envelope shape.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/testing/testRunner.hpp"
#include "backend/testing/fixtureManager.hpp"
#include "backend/system/systemManager.h"
#include "backend/backend_exception.h"
#include "backend/compiler/value.h"

// ===========================================================================
// Discovery — @test annotation parsing
// ===========================================================================

TEST(TestRunnerDiscovery, EmptySourceFindsNothing) {
	const auto out = ibTesting::DiscoverTestsInSource(wxT(""));
	EXPECT_TRUE(out.empty());
}

TEST(TestRunnerDiscovery, SingleProcedureWithMarker) {
	const wxString src = wxT(
		"// @test Заказ_создается\n"
		"Procedure TestOrder() Export\n"
		"  AssertEquals(1, 1);\n"
		"EndProcedure\n"
	);
	const auto out = ibTesting::DiscoverTestsInSource(src);
	ASSERT_EQ(out.size(), 1u);
	EXPECT_EQ(out[0].procedure, wxT("TestOrder"));
	EXPECT_EQ(out[0].name,      wxT("Заказ_создается"));
}

TEST(TestRunnerDiscovery, ProcedureWithoutMarkerIgnored) {
	const wxString src = wxT(
		"Procedure NotATest() Export\n"
		"  AssertEquals(1, 1);\n"
		"EndProcedure\n"
	);
	const auto out = ibTesting::DiscoverTestsInSource(src);
	EXPECT_TRUE(out.empty());
}

TEST(TestRunnerDiscovery, MarkerWithoutLabelDefaultsToProcName) {
	const wxString src = wxT(
		"// @test\n"
		"Procedure TestSum() Export\n"
		"EndProcedure\n"
	);
	const auto out = ibTesting::DiscoverTestsInSource(src);
	ASSERT_EQ(out.size(), 1u);
	EXPECT_EQ(out[0].procedure, wxT("TestSum"));
	EXPECT_EQ(out[0].name,      wxT("TestSum"));
}

TEST(TestRunnerDiscovery, MultipleProceduresInOneModule) {
	const wxString src = wxT(
		"// @test first\n"
		"Procedure TestA() Export\n"
		"EndProcedure\n"
		"\n"
		"// some other comment, ignored\n"
		"Procedure Helper()\n"
		"EndProcedure\n"
		"\n"
		"// @test second\n"
		"Procedure TestB() Export\n"
		"EndProcedure\n"
	);
	const auto out = ibTesting::DiscoverTestsInSource(src);
	ASSERT_EQ(out.size(), 2u);
	EXPECT_EQ(out[0].procedure, wxT("TestA"));
	EXPECT_EQ(out[0].name,      wxT("first"));
	EXPECT_EQ(out[1].procedure, wxT("TestB"));
	EXPECT_EQ(out[1].name,      wxT("second"));
}

TEST(TestRunnerDiscovery, FunctionAlsoEligible) {
	const wxString src = wxT(
		"// @test fn-test\n"
		"Function TestCompute() Export\n"
		"  Return 42;\n"
		"EndFunction\n"
	);
	const auto out = ibTesting::DiscoverTestsInSource(src);
	ASSERT_EQ(out.size(), 1u);
	EXPECT_EQ(out[0].procedure, wxT("TestCompute"));
}

// ===========================================================================
// Filter matching — `*` glob, case-insensitive
// ===========================================================================

TEST(TestRunnerFilter, EmptyPatternMatchesAll) {
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT(""), wxT("anything")));
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT("*"), wxT("anything")));
}

TEST(TestRunnerFilter, ExactNameMatches) {
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT("TestSum"), wxT("TestSum")));
	EXPECT_FALSE(ibTesting::MatchTestFilter(wxT("TestSum"), wxT("TestOther")));
}

TEST(TestRunnerFilter, PrefixWildcardMatches) {
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT("Test*"), wxT("TestSum")));
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT("Test*"), wxT("TestOther")));
	EXPECT_FALSE(ibTesting::MatchTestFilter(wxT("Test*"), wxT("OtherTest")));
}

TEST(TestRunnerFilter, SuffixWildcardMatches) {
	EXPECT_TRUE(ibTesting::MatchTestFilter(wxT("*Sum"), wxT("TestSum")));
	EXPECT_FALSE(ibTesting::MatchTestFilter(wxT("*Sum"), wxT("TestOther")));
}

// ===========================================================================
// Assertion builtins — direct C++ calls (no procunit needed)
// ===========================================================================

TEST(AssertionBuiltins, AssertEqualsPassesOnEqualValues) {
	EXPECT_NO_THROW(ibValueSystemFunction::AssertEquals(ibValue(1), ibValue(1)));
	EXPECT_NO_THROW(ibValueSystemFunction::AssertEquals(ibValue(wxT("a")), ibValue(wxT("a"))));
}

TEST(AssertionBuiltins, AssertEqualsThrowsOnMismatch) {
	bool threw = false;
	try {
		ibValueSystemFunction::AssertEquals(ibValue(1), ibValue(2), wxT("nope"));
	} catch (const ibBackendTestAssertException& err) {
		threw = true;
		EXPECT_EQ(err.GetAssertion(), wxT("AssertEquals"));
		EXPECT_EQ(err.GetMessage(),   wxT("nope"));
	}
	EXPECT_TRUE(threw);
}

TEST(AssertionBuiltins, AssertNotEqualsRoundtrip) {
	EXPECT_NO_THROW(ibValueSystemFunction::AssertNotEquals(ibValue(1), ibValue(2)));
	EXPECT_THROW(
		ibValueSystemFunction::AssertNotEquals(ibValue(1), ibValue(1)),
		ibBackendTestAssertException);
}

TEST(AssertionBuiltins, AssertTrueFalse) {
	EXPECT_NO_THROW(ibValueSystemFunction::AssertTrue(ibValue(true)));
	EXPECT_NO_THROW(ibValueSystemFunction::AssertFalse(ibValue(false)));
	EXPECT_THROW(
		ibValueSystemFunction::AssertTrue(ibValue(false)),
		ibBackendTestAssertException);
	EXPECT_THROW(
		ibValueSystemFunction::AssertFalse(ibValue(true)),
		ibBackendTestAssertException);
}

TEST(AssertionBuiltins, AssertGreaterLess) {
	EXPECT_NO_THROW(ibValueSystemFunction::AssertGreater(ibValue(5), ibValue(3)));
	EXPECT_NO_THROW(ibValueSystemFunction::AssertLess(ibValue(2), ibValue(7)));
	EXPECT_THROW(
		ibValueSystemFunction::AssertGreater(ibValue(2), ibValue(7)),
		ibBackendTestAssertException);
	EXPECT_THROW(
		ibValueSystemFunction::AssertLess(ibValue(8), ibValue(3)),
		ibBackendTestAssertException);
}

TEST(AssertionBuiltins, AssertNotNullRecognisesEmpty) {
	ibValue nonEmpty(42);
	EXPECT_NO_THROW(ibValueSystemFunction::AssertNotNull(nonEmpty));
	// Default-constructed ibValue is empty/null per ibValue::IsEmpty().
	ibValue empty;
	EXPECT_THROW(
		ibValueSystemFunction::AssertNotNull(empty),
		ibBackendTestAssertException);
}

// ===========================================================================
// Fixture manager — push/pop balance
// ===========================================================================

TEST(FixtureManager, DegradedModeNoDatabase) {
	ibTesting::ibFixtureManager mgr(nullptr);
	EXPECT_TRUE(mgr.IsDegraded());
	EXPECT_EQ(mgr.Depth(), 0u);

	const auto pushed = mgr.Push("t1");
	EXPECT_EQ(pushed, ibTesting::FixtureOutcome::UnsupportedDriver);
	EXPECT_EQ(mgr.Depth(), 1u);

	const auto popped = mgr.Pop();
	EXPECT_EQ(popped, ibTesting::FixtureOutcome::UnsupportedDriver);
	EXPECT_EQ(mgr.Depth(), 0u);
}

TEST(FixtureManager, ScopedFixtureBalancesEvenOnFlow) {
	ibTesting::ibFixtureManager mgr(nullptr);
	{
		ibTesting::ibFixtureManager::ScopedFixture frame(mgr, "outer");
		EXPECT_EQ(mgr.Depth(), 1u);
	}
	EXPECT_EQ(mgr.Depth(), 0u);
}

TEST(FixtureManager, PopWithoutPushReportsUnderflow) {
	ibTesting::ibFixtureManager mgr(nullptr);
	const auto popped = mgr.Pop();
	EXPECT_EQ(popped, ibTesting::FixtureOutcome::NoActiveTransaction);
}
