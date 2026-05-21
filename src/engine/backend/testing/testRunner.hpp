/////////////////////////////////////////////////////////////////////////////
// testRunner — functional test discovery + execution for OES.
//
// Discovery model (v1): annotation-based. Procedures marked with `// @test`
// on the line BEFORE the `Procedure …` / `Function …` keyword are
// collected. The procedure must be declared `Export` (otherwise the
// in-process ibProcUnit::CallAsProc can't reach it by name from outside
// the module). Optional human-readable test name follows the marker:
//
//   // @test Заказ_создается_с_правильными_суммами
//   Procedure TestЗаказ() Export
//     ...
//   EndProcedure
//
// Follow-up: a "TestSuite" metadata kind would let the configurator
// surface tests in the Designer tree alongside Catalogs/Documents. For
// v1 we lean on comment annotation — zero metadata schema impact, ships
// today, future-compatible (the metadata kind would emit the same
// @test marker into the module on Save).
//
// Execution: each test runs in a ScopedFixture (database transaction
// auto-rolled-back on test exit) so write side-effects don't leak across
// tests. Assertion failures (ibBackendTestAssertException) are recorded
// as `Failed`; other backend exceptions as `Error`; no throw at all is
// `Passed`. Skipped runs (e.g. filter excludes a test) are recorded so
// the report covers the full corpus.
//
// Two-DLL boundary: backend-side, no wx GUI deps.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TESTING_TEST_RUNNER_HPP_
#define _IB_TESTING_TEST_RUNNER_HPP_

#include "backend/backend.h"

#include <cstdint>
#include <string>
#include <vector>

class ibMetaData;

namespace ibTesting {

enum class TestStatus {
	Passed = 0,
	Failed,    // assertion mismatched
	Error,     // non-assertion exception (compile, runtime, etc.)
	Skipped,   // filter excluded the test
};

struct TestFailureDetail {
	wxString assertion;     // e.g. "AssertEquals"
	wxString actualText;
	wxString expectedText;
	wxString message;
	long     line = wxNOT_FOUND; // source line of the bytecode frame at throw
};

struct TestResult {
	wxString          name;       // human-readable name from @test marker
	wxString          procedure;  // exported procedure name actually invoked
	wxString          module;     // module full name (e.g. CommonModules.TestSuite_Sales)
	TestStatus        status = TestStatus::Skipped;
	std::int64_t      durationMs = 0;
	TestFailureDetail failure;    // populated on Failed/Error
};

struct TestSummary {
	std::size_t  total       = 0;
	std::size_t  passed      = 0;
	std::size_t  failed      = 0;
	std::size_t  errored     = 0;
	std::size_t  skipped     = 0;
	std::int64_t durationMs  = 0;
	bool         fixtureDegraded = false; // no DB or driver unsupported
};

struct TestRun {
	TestSummary             summary;
	std::vector<TestResult> tests;
	wxString                error;   // top-level fatal (no config, etc.)
};

struct TestRunOptions {
	wxString filter;                  // glob-ish pattern, "*" wildcard
	std::vector<wxString> moduleFilter;  // restrict to these module full names
	bool stopOnFirstFailure = false;
};

// Run all discovered tests against the live activeMetaData. Returns a
// complete report even on partial failure (top-level error populated when
// the configuration isn't loaded; tests vector populated otherwise).
//
// Thread model: called from the MCP server's stdio thread (single-thread
// of test). Internally uses appData->GetDatabaseLayer() through the
// fixture manager — same TLS slot the in-process runtime uses.
BACKEND_API TestRun RunTests(const TestRunOptions& opts);

// Discovery helper exposed for unit tests — given a module's source text,
// return the names of every procedure tagged `// @test`. Pair value is
// (procedureName, humanReadableName); humanReadableName defaults to the
// procedure name when the marker has no trailing label.
struct DiscoveredTest {
	wxString procedure;
	wxString name;
};
BACKEND_API std::vector<DiscoveredTest> DiscoverTestsInSource(const wxString& moduleSource);

// Glob match for filter — "*" matches everything, otherwise a simple
// `*` wildcard match (no character classes, case-insensitive).
BACKEND_API bool MatchTestFilter(const wxString& pattern, const wxString& name);

} // namespace ibTesting

#endif // _IB_TESTING_TEST_RUNNER_HPP_
