/////////////////////////////////////////////////////////////////////////////
// testRunner — see header for design rationale.
//
// Implementation notes:
//
// * Discovery walks every common-module / object-module / manager-module
//   in activeMetaData via GetAnyArrayObject<ibValueMetaObjectModuleBase>,
//   filtering by CLSID for the four module flavours that can carry user
//   code. For each, we parse the module's source text for `// @test`
//   markers via DiscoverTestsInSource (regex-based; CES line comments
//   start with `//` and VES with `//` too post-2024).
//
// * Execution compiles the module's full source once per test invocation
//   (cheaper than caching for v1; AOT compilation already covers the
//   repeated-recompile cost), then invokes the test procedure through
//   ibProcUnit::CallAsProc with no args. Output is recorded via
//   ibBackendException::GetLastError() for the "error" branch.
//
// * Fixture wraps each test in a ScopedFixture. When activeMetaData has
//   no live ibDatabaseLayer (Designer launched with --no-config, or the
//   driver doesn't support nested transactions), the fixture is a no-op
//   and the report flags fixtureDegraded so the runner caller can decide
//   whether to abort or accept reduced isolation.
/////////////////////////////////////////////////////////////////////////////

#include "testRunner.hpp"
#include "fixtureManager.hpp"

#include "backend/appData.h"
#include "backend/metadata.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/procUnitState.h"
#include "backend/compiler/procContext.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/session/session.h"

#include <wx/regex.h>

#include <chrono>

namespace ibTesting {

namespace {

// Steady-clock millisecond delta — wxStopWatch is wxBase-safe but we
// keep it C++-standard to dodge any wx event-loop dependency.
std::int64_t MillisSince(const std::chrono::steady_clock::time_point& start) {
	const auto now = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

// CLSIDs the test scanner considers "carries user code". Manager modules
// and form modules don't pass through the module-base hierarchy the same
// way (form modules live inside metaFormObject); for v1 we restrict to
// the four ibValueMetaObjectModuleBase subclasses that GetAnyArrayObject
// will surface.
bool IsModuleBearingClsid(const ibClassID clsid)
{
	return clsid == g_metaModuleCLSID         // object module
	    || clsid == g_metaCommonModuleCLSID   // common module
	    || clsid == g_metaManagerCLSID;       // manager module
}

// Source line number of the failure — best-effort. ibBackendTestAssertException
// doesn't itself carry a line number, but the proc-unit state's run-context
// stack does. We grab the top frame's m_lCurLine on the way out.
long CurrentFailureLine()
{
	if (auto* puState = ibSession::GetPUState()) {
		// ibSession isn't always wired (no-config mode); GetPUState
		// returns the sessionless TLS fallback in that case, which
		// still tracks the call stack for the in-flight ProcUnit run.
		if (auto* rc = puState->GetCurrentRunContext()) {
			return rc->m_lCurLine;
		}
	}
	return wxNOT_FOUND;
}

// Forward decl used by the running loop. Defined below.
TestStatus RunOneTest(ibCompileCode& compiled,
                       const DiscoveredTest& dt,
                       ibFixtureManager& fixtures,
                       TestFailureDetail& failureOut);

} // namespace

////////////////////////////////////////////////////////////////////////////
// Discovery
////////////////////////////////////////////////////////////////////////////

std::vector<DiscoveredTest> DiscoverTestsInSource(const wxString& moduleSource)
{
	std::vector<DiscoveredTest> out;

	// Regex breakdown:
	//   1. `//` followed by optional whitespace
	//   2. literal `@test`
	//   3. optional whitespace + capture of human-readable name (anything
	//      up to end-of-line)
	//   4. newline(s) — possibly with leading whitespace
	//   5. `Procedure` (VES) or `procedure` (CES — case-insensitive on
	//      keywords; the compiler accepts both) followed by capture of
	//      identifier up to `(`
	//
	// wxRegEx::CompileAdv lets us pass `wxRE_ADVANCED | wxRE_NEWLINE` so
	// `.` doesn't traverse newlines and we don't pull in a foreign regex
	// engine. We compile a single big alternation for Procedure | Function
	// since either can be a test target (Function with no return is
	// fine — return value is discarded).
	wxRegEx rx;
	const wxString pattern = wxT(
		"//[[:space:]]*@test[[:space:]]*([^\r\n]*)[\r\n]+"
		"[[:space:]]*"
		"([Pp][Rr][Oo][Cc][Ee][Dd][Uu][Rr][Ee]|[Ff][Uu][Nn][Cc][Tt][Ii][Oo][Nn])"
		"[[:space:]]+([A-Za-z_][A-Za-z_0-9]*)"
	);
	if (!rx.Compile(pattern, wxRE_ADVANCED | wxRE_NEWLINE)) {
		return out;
	}

	wxString cursor = moduleSource;
	while (rx.Matches(cursor)) {
		size_t start = 0, len = 0;
		rx.GetMatch(&start, &len, 0);

		DiscoveredTest dt;
		dt.name      = rx.GetMatch(cursor, 1).Trim(true).Trim(false);
		dt.procedure = rx.GetMatch(cursor, 3);

		// Empty trailing label → fall back to procedure name.
		if (dt.name.IsEmpty()) dt.name = dt.procedure;

		out.push_back(std::move(dt));

		// Advance past this match — the regex engine doesn't track a
		// cursor across calls in this version of wx.
		cursor = cursor.Mid(start + len);
	}

	return out;
}

bool MatchTestFilter(const wxString& pattern, const wxString& name)
{
	if (pattern.IsEmpty() || pattern == wxT("*")) return true;

	// One-pass glob with a single `*` (the only wildcard v1 supports).
	// Multiple `*` segments compose by recursive Match calls.
	const int starPos = pattern.Find(wxT('*'));
	if (starPos == wxNOT_FOUND) {
		return name.IsSameAs(pattern, false);
	}
	const wxString prefix = pattern.Left(starPos);
	const wxString rest   = pattern.Mid(starPos + 1);

	if (!name.StartsWith(prefix)) return false;
	// Try every split point in the remaining haystack.
	const wxString tail = name.Mid(prefix.length());
	for (size_t i = 0; i <= tail.length(); ++i) {
		if (MatchTestFilter(rest, tail.Mid(i))) return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////
// Execution
////////////////////////////////////////////////////////////////////////////

namespace {

TestStatus RunOneTest(ibCompileCode& compiled,
                       const DiscoveredTest& dt,
                       ibFixtureManager& fixtures,
                       TestFailureDetail& failureOut)
{
	// Each test gets a fresh fixture frame — DB writes auto-rolled-back
	// on scope exit unless the test explicitly commits (rare).
	ibFixtureManager::ScopedFixture frame(fixtures, "test");
	(void)frame.PushOutcome();  // surfaced through summary.fixtureDegraded

	ibProcUnit pu;
	try {
		// Bind extern/context slots from the compile-context so the
		// procedure under test can reach metadata/system context just
		// like a regular runtime call would. Same shape as codeRunner.
		ibByteBinder binder = compiled.CreateBinder();
		pu.Execute(compiled.m_cByteCode, binder);

		// CallAsProc returns false if the procedure isn't exported or
		// doesn't exist — treat as error so the operator can see why
		// the test didn't run.
		const bool ok = pu.CallAsProc(dt.procedure, nullptr, 0);
		if (!ok) {
			failureOut.assertion = wxT("procedure not found");
			failureOut.message   = wxT("Procedure '") + dt.procedure +
				wxT("' is not exported or compile binding failed");
			failureOut.line      = CurrentFailureLine();
			return TestStatus::Error;
		}
	}
	catch (const ibBackendTestAssertException& err) {
		failureOut.assertion    = err.GetAssertion();
		failureOut.actualText   = err.GetActualText();
		failureOut.expectedText = err.GetExpectedText();
		failureOut.message      = err.GetMessage();
		failureOut.line         = CurrentFailureLine();
		return TestStatus::Failed;
	}
	catch (const ibBackendException& err) {
		failureOut.assertion = wxT("exception");
		failureOut.message   = err.GetErrorDescription();
		failureOut.line      = CurrentFailureLine();
		return TestStatus::Error;
	}
	catch (...) {
		failureOut.assertion = wxT("exception");
		failureOut.message   = wxT("<non-backend exception>");
		failureOut.line      = CurrentFailureLine();
		return TestStatus::Error;
	}

	return TestStatus::Passed;
}

} // namespace

TestRun RunTests(const TestRunOptions& opts)
{
	TestRun out;
	const auto wallStart = std::chrono::steady_clock::now();

	if (activeMetaData == nullptr) {
		out.error = wxT("Test runner requires a loaded configuration "
		             "(activeMetaData is null)");
		return out;
	}

	// DB layer may be unavailable in some headless modes — fixtures will
	// degrade to no-op and the summary surfaces it.
	std::shared_ptr<ibDatabaseLayer> db = ibApplicationData::GetDatabaseLayer();
	ibFixtureManager fixtures(db.get());

	// Walk every module-bearing object once.
	std::vector<ibValueMetaObjectModuleBase*> modules =
		activeMetaData->GetAnyArrayObject<ibValueMetaObjectModuleBase>(
			{ g_metaModuleCLSID, g_metaCommonModuleCLSID, g_metaManagerCLSID },
			/*use_child_filter=*/true);

	for (ibValueMetaObjectModuleBase* mod : modules) {
		if (mod == nullptr) continue;

		const wxString moduleFullName = mod->GetFullName();

		// Module-scope filter — restrict to the caller's chosen subset.
		if (!opts.moduleFilter.empty()) {
			bool match = false;
			for (const auto& m : opts.moduleFilter) {
				if (moduleFullName == m) { match = true; break; }
			}
			if (!match) continue;
		}

		const wxString src = mod->GetModuleText();
		std::vector<DiscoveredTest> discovered = DiscoverTestsInSource(src);
		if (discovered.empty()) continue;

		// Compile the module ONCE per module discovery (each test will
		// receive a fresh ProcUnit inside RunOneTest). Compile failures
		// surface every test in this module as Error with the compile
		// message so the operator can fix the module-level break.
		ibCompileCode cc(mod->GetModuleName(), mod->GetDocPath(), false);
		bool compileOk = true;
		wxString compileError;
		try {
			cc.Compile(src);
		}
		catch (const ibBackendException& err) {
			compileOk    = false;
			compileError = err.GetErrorDescription();
		}
		catch (...) {
			compileOk    = false;
			compileError = wxT("<unknown compile failure>");
		}

		for (const DiscoveredTest& dt : discovered) {
			TestResult result;
			result.name      = dt.name;
			result.procedure = dt.procedure;
			result.module    = moduleFullName;

			// Filter — empty/`*` matches everything.
			if (!MatchTestFilter(opts.filter, dt.name) &&
			    !MatchTestFilter(opts.filter, dt.procedure)) {
				result.status = TestStatus::Skipped;
				out.tests.push_back(std::move(result));
				continue;
			}

			const auto testStart = std::chrono::steady_clock::now();

			if (!compileOk) {
				result.status              = TestStatus::Error;
				result.failure.assertion   = wxT("compile failed");
				result.failure.message     = compileError;
				result.durationMs          = MillisSince(testStart);
				out.tests.push_back(std::move(result));
				continue;
			}

			TestFailureDetail failure;
			result.status     = RunOneTest(cc, dt, fixtures, failure);
			result.failure    = std::move(failure);
			result.durationMs = MillisSince(testStart);

			const bool isFail =
				result.status == TestStatus::Failed ||
				result.status == TestStatus::Error;

			out.tests.push_back(std::move(result));

			if (opts.stopOnFirstFailure && isFail) {
				goto done;
			}
		}
	}

done:
	// Build summary
	out.summary.fixtureDegraded = fixtures.IsDegraded();
	out.summary.total           = out.tests.size();
	for (const auto& r : out.tests) {
		switch (r.status) {
		case TestStatus::Passed:  ++out.summary.passed;  break;
		case TestStatus::Failed:  ++out.summary.failed;  break;
		case TestStatus::Error:   ++out.summary.errored; break;
		case TestStatus::Skipped: ++out.summary.skipped; break;
		}
	}
	out.summary.durationMs = MillisSince(wallStart);

	return out;
}

} // namespace ibTesting
