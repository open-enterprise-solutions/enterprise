// =============================================================================
// Scheduled and background jobs at run time — BackgroundJob and ScheduledJobs.Predefined (integration scope).
//
// Two halves, each over a live application (appData env + a SQLite pool), as the tenancy target is:
//
//   BACKGROUND — a run says where it stands: Active, then Completed, Failed or Cancelled. A rented run is not
//   in the application's list (GetBackgroundJobs, FindBackgroundJob). A second run of the same method under the same key is refused
//   while the first is active. Runs are rented (ibJobTenancy::Tenant) and their bodies are C++, so the test
//   needs no compiled runtime; a standalone run's place in the list is checked on a stand.
//
//   SCHEDULED — a predefined job's row in ScheduledJobs.Predefined starts from what the job DECLARES, and
//   Write stores the change in sys_job, where the next read finds it.
//
// SKIPS (not fails) if the headless environment cannot come up. Its own target, so that bring-up cannot
// perturb the main suite.
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <wx/filename.h>
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appDataCtorToken.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnitState.h"            // ibRunCancelled — what a body polls
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/job/jobManager.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaScheduledJobObject.h"
#include "backend/propertyManager/property/propertyBoolean.h"
#include "backend/session/session.h"
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueBackgroundJob.h"
#include "backend/system/value/valueJob.h"

// sys_job is the application's own table, made when a base opens; a configuration run in memory never
// opens one, so the test makes it the way the opening does.
struct JobSchemaAccess {
	static void CreateTableJob() { ibApplicationInstance::CreateTableJob(); }
};

// A requested run of a C++ body — what RunBackground starts, without a module to name.
struct JobLaunchAccess {
	static std::shared_ptr<ibBackgroundRun> Launch(ibJobManager& manager, ibJobManager::ibBackgroundBody body,
		const ibBackgroundRequest& request) {
		return manager.Launch(std::move(body), request, request.m_description, ibJobTenancy::Tenant);
	}
};

namespace {

// --------------------------------------------------------------------------
// A live application with a session bound, as a form's session is
// --------------------------------------------------------------------------

struct JobRuntimeFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	std::shared_ptr<ibSession>             parent;
	std::unique_ptr<ibSessionScope>        bound;
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		if (ibApplicationInstance::GetSessionRegistry() == nullptr)
			GTEST_SKIP() << "no session registry after CreateAppDataEnv";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, 2, /*minIdle=*/0);

		parent = std::make_shared<ibSession>(ibString(wxT("job-runtime-parent")), ibSessionKind::Enterprise);
		bound  = std::make_unique<ibSessionScope>(parent.get());
		ready  = true;
	}

	void TearDown() override {
		bound.reset();
		parent.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	static ibBackgroundRequest Request(const ibString& key, const ibString& description = wxT("a test run")) {
		ibBackgroundRequest request;
		request.m_method = wxT("Tests.Run");
		request.m_key = key;
		request.m_description = description;
		return request;
	}
};

ibValue StateMember(ibBackgroundJobState state)
{
	return ibValue::CreateEnumObject<ibValueEnumBackgroundJobState>(state);
}

ibValue Prop(ibValue& value, const ibString& name)
{
	ibValue result;
	const long number = value.FindProp(name);
	EXPECT_NE(number, wxNOT_FOUND) << name;
	value.GetPropVal(number, result);
	return result;
}

ibValue Func(ibValue& value, const ibString& name, ibValue** params = nullptr, long count = 0)
{
	ibValue result;
	const long number = value.FindMethod(name);
	EXPECT_NE(number, wxNOT_FOUND) << name;
	value.CallAsFunc(number, result, params, count);
	return result;
}

} // namespace

// --------------------------------------------------------------------------
// Background — where a run stands
// --------------------------------------------------------------------------

TEST_F(JobRuntimeFix, ABackgroundJobThatReturnsIsCompleted)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	auto run = JobLaunchAccess::Launch(manager, [](ibSession*) { return ibValue(42); },
		Request(wxT("done"), wxT("returns at once")));

	ASSERT_TRUE(run->Wait(10000));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Completed);
	EXPECT_TRUE(run->Error().IsEmpty()) << run->Error();
	EXPECT_EQ(run->Result().GetInteger(), 42);
	EXPECT_FALSE(run->Begin().IsEmpty());
	EXPECT_FALSE(run->End().IsEmpty()) << "an ended run says when it ended";
	EXPECT_LE(run->Begin(), run->End());

	ibValue job(new ibValueBackgroundJob(run));
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Completed));
	EXPECT_EQ(Prop(job, wxT("Key")).GetString(), ibString(wxT("done")));
	EXPECT_EQ(Prop(job, wxT("Description")).GetString(), ibString(wxT("returns at once")));
	EXPECT_EQ(Prop(job, wxT("MethodName")).GetString(), ibString(wxT("Tests.Run")));
	EXPECT_TRUE(Func(job, wxT("Error")).GetString().IsEmpty());

	manager.Stop();
}

TEST_F(JobRuntimeFix, ABackgroundJobThatThrowsIsFailedAndSaysWhy)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	auto run = JobLaunchAccess::Launch(manager, [](ibSession*) -> ibValue {
			ibBackendCoreException::Error(wxT("the warehouse did not answer"));
			return ibValue();
		}, Request(wxT("broken")));

	ASSERT_TRUE(run->Wait(10000));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Failed);
	EXPECT_NE(run->Error().Find(wxT("the warehouse did not answer")), wxNOT_FOUND) << run->Error();

	ibValue job(new ibValueBackgroundJob(run));
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Failed));
	EXPECT_NE(Func(job, wxT("Error")).GetString().Find(wxT("the warehouse did not answer")), wxNOT_FOUND);

	manager.Stop();
}

TEST_F(JobRuntimeFix, ABackgroundJobThatIsCancelledIsCancelledNotFailed)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	std::atomic<bool> started { false };
	auto run = JobLaunchAccess::Launch(manager, [&started](ibSession* session) -> ibValue {
			started.store(true);
			// What the interpreter does between opcodes, done by hand: hear the cancel and unwind.
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!ibRunCancelled(session->RunState()) && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			ibBackendInterruptException::Error();
			return ibValue();
		}, Request(wxT("long")));

	while (!started.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Active);

	ibValue job(new ibValueBackgroundJob(run));
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Active));
	EXPECT_TRUE(Prop(job, wxT("End")).GetDate().IsEmpty()) << "a run in flight has not ended";

	// Through the script's door — Cancel() is the procedure a script calls.
	ASSERT_TRUE(job.CallAsProc(job.FindMethod(wxT("Cancel")), nullptr, 0));

	ibValue timeout(10000);
	ibValue* params[] = { &timeout };
	EXPECT_TRUE(Func(job, wxT("Wait"), params, 1).GetBoolean());
	EXPECT_EQ(run->State(), ibBackgroundJobState_Cancelled)
		<< "a cancelled run did nothing wrong — it must not read as Failed";
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Cancelled));

	manager.Stop();
}

// --------------------------------------------------------------------------
// Background — held, found, and one per key
// --------------------------------------------------------------------------

TEST_F(JobRuntimeFix, ARentedRunIsNotListedNorFoundByItsUUID)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	std::atomic<bool> release { false };
	auto run = JobLaunchAccess::Launch(manager, [&release](ibSession*) {
			while (!release.load())
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			return ibValue();
		}, Request(wxT("rented")));

	// EXPECT, not ASSERT: leaving here before `release` left the run spinning, and the test hung instead of failing.
	EXPECT_TRUE(run->UUID().isValid()) << "a run whose session came up is named by it";
	EXPECT_EQ(manager.FindBackgroundJob(run->UUID()), nullptr)
		<< "a read rented for a form is the form's business, not the application's list";
	EXPECT_TRUE(manager.GetBackgroundJobs().empty());

	release.store(true);
	ASSERT_TRUE(run->Wait(10000));
	manager.Tick();
	EXPECT_TRUE(manager.GetBackgroundJobs().empty());

	manager.Stop();
}

TEST_F(JobRuntimeFix, ASecondRunUnderTheSameKeyIsRefusedWhileTheFirstIsActive)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	std::atomic<bool> release { false };
	const ibBackgroundRequest request = Request(wxT("upload"));
	auto first = JobLaunchAccess::Launch(manager, [&release](ibSession*) {
			while (!release.load())
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			return ibValue();
		}, request);

	EXPECT_THROW(JobLaunchAccess::Launch(manager, [](ibSession*) { return ibValue(); }, request), ibBackendException)
		<< "a double click must not start the same work twice";

	std::shared_ptr<ibBackgroundRun> other;
	EXPECT_NO_THROW(other = JobLaunchAccess::Launch(manager, [](ibSession*) { return ibValue(); },
		Request(wxT("download")))) << "another key is another piece of work";

	release.store(true);
	ASSERT_TRUE(first->Wait(10000));
	if (other) other->Wait(10000);

	std::shared_ptr<ibBackgroundRun> again;
	EXPECT_NO_THROW(again = JobLaunchAccess::Launch(manager, [](ibSession*) { return ibValue(); }, request))
		<< "the key is free once its run ended";
	if (again) again->Wait(10000);

	manager.Stop();
}

// --------------------------------------------------------------------------
// Scheduled — a predefined job's row, read and written
// --------------------------------------------------------------------------

namespace {

struct PredefinedJobsFix : ::testing::Test {
	wxInitializer m_wxInit;
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectScheduledJob* cleanup = nullptr;    // declared off
	ibValueMetaObjectScheduledJob* backup = nullptr;     // declared on
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		dbPath = wxFileName::CreateTempFileName(wxT("oesjob"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/2, /*minIdle=*/0);

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		cleanup = dynamic_cast<ibValueMetaObjectScheduledJob*>(cfg->CreateMetaObject(g_metaScheduledJobCLSID, root, false));
		backup = dynamic_cast<ibValueMetaObjectScheduledJob*>(cfg->CreateMetaObject(g_metaScheduledJobCLSID, root, false));
		ASSERT_NE(cleanup, nullptr);
		ASSERT_NE(backup, nullptr);
		cleanup->SetName(wxT("Cleanup"));
		backup->SetName(wxT("Backup"));
		auto* use = dynamic_cast<ibPropertyBoolean*>(cleanup->GetProperty(ibT("Use")));
		ASSERT_NE(use, nullptr);
		use->SetValue(false);

		// Before the run, as a base's opening has it: running the configuration registers its jobs, and
		// registering reads their settings.
		JobSchemaAccess::CreateTableJob();
		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";
		ready = true;
	}

	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (db) db->Close();
		db.reset();
		if (!dbPath.IsEmpty()) {
			if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
			if (wxFileExists(dbPath + wxT("-wal"))) wxRemoveFile(dbPath + wxT("-wal"));
			if (wxFileExists(dbPath + wxT("-shm"))) wxRemoveFile(dbPath + wxT("-shm"));
		}
	}

	// The row of one job in ScheduledJobs.Predefined, read afresh — a new collection reads the base again.
	ibValue Row(const ibString& name) {
		ibValuePtr<ibValuePredefinedJobs> predefined(new ibValuePredefinedJobs(cfg.get()));
		for (const ibValue& row : predefined->Values()) {
			ibValue held = row;
			if (Prop(held, wxT("Name")).GetString() == name)
				return held;
		}
		ADD_FAILURE() << "no row for " << name;
		return ibValue();
	}
};

void SetProp(ibValue& value, const ibString& name, const ibValue& to)
{
	const long number = value.FindProp(name);
	ASSERT_NE(number, wxNOT_FOUND) << name;
	value.SetPropVal(number, to);
}

} // namespace

TEST_F(PredefinedJobsFix, APredefinedJobStartsFromWhatItDeclares)
{
	if (!ready) GTEST_SKIP();

	ibValue off = Row(wxT("Cleanup"));
	ibValue on = Row(wxT("Backup"));
	EXPECT_FALSE(Prop(off, wxT("Active")).GetBoolean())
		<< "a base that never wrote the job's row runs it as declared — and it is declared off";
	EXPECT_TRUE(Prop(on, wxT("Active")).GetBoolean());
	EXPECT_TRUE(Prop(off, wxT("LastRun")).GetDate().IsEmpty());
	EXPECT_TRUE(Prop(off, wxT("LastJob")).IsEmpty()) << "a job that never ran here has no last run to show";
}

TEST_F(PredefinedJobsFix, WriteStoresTheSwitchAndTheScheduleWhereTheNextReadFindsThem)
{
	if (!ready) GTEST_SKIP();

	ibValue job = Row(wxT("Backup"));
	SetProp(job, wxT("Active"), ibValue(false));
	ibJobScheduleDescription schedule;
	schedule.m_intervalSeconds = 600;
	SetProp(job, wxT("Schedule"), ibValue(new ibValueSchedule(schedule)));
	EXPECT_TRUE(Func(job, wxT("Write")).GetBoolean());

	const ibJobManager::ibJobSettings stored = ibJobManager::ReadSharedSettings(backup->GetGuid());
	ASSERT_TRUE(stored.m_found) << "Write must leave a row in sys_job";
	EXPECT_FALSE(stored.m_active) << "a switch written off is read back off";
	EXPECT_EQ(stored.m_schedule.m_intervalSeconds, 600);

	ibValue again = Row(wxT("Backup"));
	EXPECT_FALSE(Prop(again, wxT("Active")).GetBoolean());
	const ibValue readValue = Prop(again, wxT("Schedule"));   // held: the pointer below is into it
	ibValueSchedule* read = nullptr;
	ASSERT_TRUE(readValue.ConvertToValue(read));
	ASSERT_NE(read, nullptr);
	EXPECT_EQ(read->GetSchedule().m_intervalSeconds, 600);
}
