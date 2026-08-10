// =============================================================================
// Frontend GUI test harness — the shared "backend mock" every frontend test
// builds on.
//
// Unlike the pure backend suites (which only need wxBase via a wxInitializer),
// the DESKTOP frontend is real wxWidgets: ibVisualHost derives from
// wxScrolledCanvas and every control is a wxWindow subclass. So a frontend test
// needs a LIVE GUI wxApp (toolkit init + event machinery), not just wxBase.
//
// Two pieces:
//   * ibWxGuiEnvironment — a process-wide gtest Environment that brings the GUI
//     toolkit up ONCE (wxEntryStart / wxEntryCleanup are not re-entrant).
//   * FrontendRuntimeFix — a per-test fixture that stands up a runtime-mode
//     appData env + a SQLite :memory: pool on the single master connection,
//     exactly like TempDbSqliteFix. Any frontend test derives from this.
//
// Everything SKIPs (never fails) when the GUI toolkit or the headless env can't
// come up — a display-less CI box lands on GTEST_SKIP instead of reddening the
// suite. Locally (a real desktop session) it runs for real.
// =============================================================================

#pragma once

#include <gtest/gtest.h>

#include <memory>

#include <wx/app.h>
#include <wx/init.h>
#include <wx/debug.h>   // wxSetAssertHandler
#include <wx/log.h>     // wxLogStderr — the default wxLogGui is a MODAL flush

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

// Minimal GUI wxApp for the test process. OnInit does nothing — the tests drive
// the frontend directly rather than through a main frame's startup flow.
class ibFrontendTestApp : public wxApp {
public:
	bool OnInit() override { return true; }
	int  OnExit() override { return 0; }
};

// Process-wide GUI toolkit lifecycle. gtest_main owns main(), so we can't
// wxEntryStart there — instead we register this Environment (see the
// AddGlobalTestEnvironment hook in the test TU) and gtest calls SetUp before
// the first test and TearDown after the last.
class ibWxGuiEnvironment : public ::testing::Environment {
public:
	void SetUp() override {
		// Headless: no modal dialogs. A wxASSERT or a CRT heap/assert report in a
		// Debug build otherwise pops a blocking window that hangs the run. Route
		// everything to stderr and drop wx asserts so the suite runs to the end
		// and a failing test reports where, instead of freezing on a dialog.
#ifdef _WIN32
		_CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
		SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
		wxSetAssertHandler(nullptr);   // wxASSERT -> no-op (no dialog)

		wxApp::SetInstance(new ibFrontendTestApp());
		int argc = 0;
		m_ok = wxEntryStart(argc, static_cast<wxChar**>(nullptr));
		if (m_ok) {
			// The last dialog left, and the one that actually hung CI: a GUI wxApp
			// installs wxLogGui, which does not print warnings — it BUFFERS them and
			// pours the whole batch into a modal wxLogDialog when the log target goes
			// away (wxApp::CleanUp -> wxLog::SetActiveTarget(nullptr) -> Flush). That
			// happens inside wxEntryCleanup below, AFTER the last test passed, so the
			// suite is green and the process never exits. Any warning at all is enough
			// (libpng's "iCCP: known incorrect sRGB profile" on our icons is one).
			// stderr has no OK button.
			delete wxLog::SetActiveTarget(new wxLogStderr());

			m_ok = wxTheApp->CallOnInit();
		}
	}
	void TearDown() override {
		if (m_ok) {
			wxTheApp->OnExit();
			wxEntryCleanup();
		}
	}
	bool IsOk() const { return m_ok; }

	// Set by the registration hook in the test TU so fixtures can consult the
	// toolkit's health before touching any wx object.
	static ibWxGuiEnvironment* s_instance;

private:
	bool m_ok = false;
};

// The shared backend harness: runtime-mode appData env + a SQLite :memory:
// pool. maxSize=1 / minIdle=0 so the pool always hands out the SINGLE master
// connection (a :memory: clone would be a separate empty DB). Mirrors
// TempDbSqliteFix — the difference is the GUI wxApp requirement above.
struct FrontendRuntimeFix : ::testing::Test {
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	void SetUp() override {
		if (ibWxGuiEnvironment::s_instance == nullptr || !ibWxGuiEnvironment::s_instance->IsOk())
			GTEST_SKIP() << "GUI wxApp unavailable (no display / headless)";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};
