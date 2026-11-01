// =============================================================================
// Scheduled and background jobs at run time — BackgroundJobs and ScheduledJobs' verbs (integration scope).
//
// Two halves, each over a live application (appData env + a SQLite pool), as the tenancy target is:
//
//   BACKGROUND — a run started with a label (BackgroundJobs.Execute) is LISTED: found again by its UUID
//   and in the list, for a while after it ended, and it says where it stands — Active, then Completed,
//   Failed or Canceled. A second run of the same method under the same key is refused while the first
//   is active. Runs are rented (ibJobTenancy::Tenant) so the test needs no compiled runtime: being listed
//   is the label's business, not the tenancy's.
//
//   SCHEDULED — a predefined job read through ScheduledJob starts from what it DECLARES, and Write
//   stores the change in sys_job, where the next read finds it. Found by its metaobject and by its UUID,
//   listed with a filter, and every verb that does not apply to a kind says so instead of doing nothing.
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
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueScheduledJob.h"

// sys_job is the application's own table, made when a base opens; a configuration run in memory never
// opens one, so the test makes it the way the opening does.
struct JobSchemaAccess {
	static void CreateTableJob() { ibApplicationInstance::CreateTableJob(); }
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

	static ibBackgroundLabel Label(const ibString& key, const ibString& description = wxT("a test run")) {
		ibBackgroundLabel label;
		label.m_method = wxT("Tests.Run");
		label.m_key = key;
		label.m_description = description;
		return label;
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

} // namespace

// --------------------------------------------------------------------------
// Background — where a run stands
// --------------------------------------------------------------------------

TEST_F(JobRuntimeFix, ABackgroundJobThatReturnsIsCompleted)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	const ibBackgroundLabel label = Label(wxT("done"), wxT("returns at once"));
	auto run = manager.StartBackground([](ibSession*) { return ibValue(42); },
		wxT("returns at once"), ibJobTenancy::Tenant, &label);

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
	EXPECT_TRUE(Prop(job, wxT("ErrorInfo")).GetString().IsEmpty());

	manager.Stop();
}

TEST_F(JobRuntimeFix, ABackgroundJobThatThrowsIsFailedAndSaysWhy)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	const ibBackgroundLabel label = Label(wxT("broken"));
	auto run = manager.StartBackground([](ibSession*) -> ibValue {
			ibBackendCoreException::Error(wxT("the warehouse did not answer"));
			return ibValue();
		}, wxT("throws"), ibJobTenancy::Tenant, &label);

	ASSERT_TRUE(run->Wait(10000));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Failed);
	EXPECT_NE(run->Error().Find(wxT("the warehouse did not answer")), wxNOT_FOUND) << run->Error();

	ibValue job(new ibValueBackgroundJob(run));
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Failed));
	EXPECT_NE(Prop(job, wxT("ErrorInfo")).GetString().Find(wxT("the warehouse did not answer")), wxNOT_FOUND);

	manager.Stop();
}

TEST_F(JobRuntimeFix, ABackgroundJobThatIsCancelledIsCanceledNotFailed)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	std::atomic<bool> started { false };
	const ibBackgroundLabel label = Label(wxT("long"));
	auto run = manager.StartBackground([&started](ibSession* session) -> ibValue {
			started.store(true);
			// What the interpreter does between opcodes, done by hand: hear the cancel and unwind.
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!ibRunCancelled(session->RunState()) && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			ibBackendInterruptException::Error();
			return ibValue();
		}, wxT("waits to be cancelled"), ibJobTenancy::Tenant, &label);

	while (!started.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Active);

	ibValue job(new ibValueBackgroundJob(run));
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Active));
	EXPECT_TRUE(Prop(job, wxT("End")).GetDate().IsEmpty()) << "a run in flight has not ended";

	// Through the script's door — Cancel() is the procedure a script calls.
	ASSERT_TRUE(job.CallAsProc(job.FindMethod(wxT("Cancel")), nullptr, 0));

	ibValue timeout(10), waited;
	ibValue* params[] = { &timeout };
	ASSERT_TRUE(job.CallAsFunc(job.FindMethod(wxT("WaitForExecutionCompletion")), waited, params, 1));
	EXPECT_EQ(run->State(), ibBackgroundJobState_Canceled)
		<< "a cancelled run did nothing wrong — it must not read as Failed";
	EXPECT_TRUE(Prop(job, wxT("State")) == StateMember(ibBackgroundJobState_Canceled));

	manager.Stop();
}

// --------------------------------------------------------------------------
// Background — listed, found, and one per key
// --------------------------------------------------------------------------

TEST_F(JobRuntimeFix, ALabelledRunIsListedAndFoundByItsUUIDAfterItEnded)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	const ibBackgroundLabel label = Label(wxT("found"));
	auto listed = manager.StartBackground([](ibSession*) { return ibValue(); },
		wxT("listed"), ibJobTenancy::Tenant, &label);
	auto unlisted = manager.StartBackground([](ibSession*) { return ibValue(); },
		wxT("a form's read"), ibJobTenancy::Tenant);
	ASSERT_TRUE(listed->Wait(10000));
	ASSERT_TRUE(unlisted->Wait(10000));

	// The tick lets a finished run's session go; the listed handle must outlive that.
	manager.Tick();

	EXPECT_EQ(manager.FindBackgroundRun(listed->UUID()), listed);
	EXPECT_EQ(manager.FindBackgroundRun(unlisted->UUID()), nullptr)
		<< "a run started without a label is its caller's business, not the list's";

	const auto runs = manager.BackgroundRuns();
	ASSERT_EQ(runs.size(), 1u);
	EXPECT_EQ(runs.front(), listed);

	manager.Stop();
}

TEST_F(JobRuntimeFix, ASecondRunUnderTheSameKeyIsRefusedWhileTheFirstIsActive)
{
	if (!ready) GTEST_SKIP();
	ibJobManager manager(ib::AppDataCtorToken{});

	std::atomic<bool> release { false };
	const ibBackgroundLabel label = Label(wxT("upload"));
	auto first = manager.StartBackground([&release](ibSession*) {
			while (!release.load())
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			return ibValue();
		}, wxT("first"), ibJobTenancy::Tenant, &label);

	EXPECT_THROW(manager.StartBackground([](ibSession*) { return ibValue(); },
		wxT("second"), ibJobTenancy::Tenant, &label), ibBackendException)
		<< "a double click must not start the same work twice";

	const ibBackgroundLabel other = Label(wxT("download"));
	std::shared_ptr<ibBackgroundRun> third;
	EXPECT_NO_THROW(third = manager.StartBackground([](ibSession*) { return ibValue(); },
		wxT("third"), ibJobTenancy::Tenant, &other)) << "another key is another piece of work";

	release.store(true);
	ASSERT_TRUE(first->Wait(10000));
	if (third) third->Wait(10000);

	std::shared_ptr<ibBackgroundRun> again;
	EXPECT_NO_THROW(again = manager.StartBackground([](ibSession*) { return ibValue(); },
		wxT("again"), ibJobTenancy::Tenant, &label)) << "the key is free once its run ended";
	if (again) again->Wait(10000);

	manager.Stop();
}

// --------------------------------------------------------------------------
// Scheduled — a predefined job, read and written through ScheduledJob
// --------------------------------------------------------------------------

namespace {

struct ScheduledJobFix : ::testing::Test {
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

	ibValue Find(ibValueMetaObjectScheduledJob* job) {
		return ibValueScheduledJob::FindPredefined(cfg.get(), ibValue(job));
	}
};

void SetProp(ibValue& value, const ibString& name, const ibValue& to)
{
	const long number = value.FindProp(name);
	ASSERT_NE(number, wxNOT_FOUND) << name;
	value.SetPropVal(number, to);
}

void Call(ibValue& value, const ibString& name)
{
	const long number = value.FindMethod(name);
	ASSERT_NE(number, wxNOT_FOUND) << name;
	value.CallAsProc(number, nullptr, 0);
}

} // namespace

TEST_F(ScheduledJobFix, APredefinedJobStartsFromWhatItDeclares)
{
	if (!ready) GTEST_SKIP();

	ibValue off = Find(cleanup);
	ibValue on = Find(backup);
	EXPECT_FALSE(Prop(off, wxT("Use")).GetBoolean())
		<< "a base that never wrote the job's row runs it as declared — and it is declared off";
	EXPECT_TRUE(Prop(on, wxT("Use")).GetBoolean());
	EXPECT_TRUE(Prop(off, wxT("Predefined")).GetBoolean());
	EXPECT_EQ(Prop(off, wxT("Key")).GetString(), ibString(wxT("Cleanup")));
	EXPECT_TRUE(Prop(off, wxT("LastRun")).GetDate().IsEmpty());
	EXPECT_TRUE(Prop(off, wxT("LastJob")).IsEmpty()) << "a job that never ran here has no last run to show";
}

TEST_F(ScheduledJobFix, WriteStoresTheSwitchAndTheScheduleWhereTheNextReadFindsThem)
{
	if (!ready) GTEST_SKIP();

	ibValue job = Find(backup);
	SetProp(job, wxT("Use"), ibValue(false));
	ibJobScheduleDescription schedule;
	schedule.m_intervalSeconds = 600;
	SetProp(job, wxT("Schedule"), ibValue(new ibValueSchedule(schedule)));
	Call(job, wxT("Write"));

	const ibJobManager::ibJobSettings stored = ibJobManager::ReadSharedSettings(backup->GetGuid());
	ASSERT_TRUE(stored.m_found) << "Write must leave a row in sys_job";
	EXPECT_FALSE(stored.m_active);
	EXPECT_EQ(stored.m_schedule.m_intervalSeconds, 600);

	ibValue again = Find(backup);
	EXPECT_FALSE(Prop(again, wxT("Use")).GetBoolean());
	const ibValue readValue = Prop(again, wxT("Schedule"));   // held: the pointer below is into it
	ibValueSchedule* read = nullptr;
	ASSERT_TRUE(readValue.ConvertToValue(read));
	ASSERT_NE(read, nullptr);
	EXPECT_EQ(read->GetSchedule().m_intervalSeconds, 600);
}

TEST_F(ScheduledJobFix, APredefinedJobIsFoundByItsUUIDAndListedByAFilter)
{
	if (!ready) GTEST_SKIP();

	ibValue byUuid = ibValueScheduledJob::FindByUUID(cfg.get(), ibValue(new ibValueGuid(cleanup->GetGuid())));
	ASSERT_FALSE(byUuid.IsEmpty());
	EXPECT_EQ(Prop(byUuid, wxT("Key")).GetString(), ibString(wxT("Cleanup")));

	ibValue byText = ibValueScheduledJob::FindByUUID(cfg.get(), ibValue(ibGuid(cleanup->GetGuid()).str()));
	EXPECT_FALSE(byText.IsEmpty()) << "a UUID's text is a UUID too";

	EXPECT_TRUE(ibValueScheduledJob::FindByUUID(cfg.get(), ibValue(new ibValueGuid(ibGuid(ibGuid::newGuid())))).IsEmpty())
		<< "no such job is an answer, not an error";

	ibValueArray* all = nullptr;
	ibValue every = ibValueScheduledJob::GetScheduledJobs(cfg.get(), ibValue());
	ASSERT_TRUE(every.ConvertToValue(all));
	EXPECT_EQ(all->Count(), 2u);

	ibValuePtr<ibValueStructure> filter(new ibValueStructure());
	filter->Insert(ibValue(ibString(wxT("Use"))), ibValue(true));
	ibValueArray* used = nullptr;
	ibValue usedValue = ibValueScheduledJob::GetScheduledJobs(cfg.get(), ibValue(filter));
	ASSERT_TRUE(usedValue.ConvertToValue(used));
	ASSERT_EQ(used->Count(), 1u);
	ibValue only = used->Values().front();
	EXPECT_EQ(Prop(only, wxT("Key")).GetString(), ibString(wxT("Backup")));

	ibValuePtr<ibValueStructure> misspelt(new ibValueStructure());
	misspelt->Insert(ibValue(ibString(wxT("Usage"))), ibValue(true));
	EXPECT_THROW(ibValueScheduledJob::GetScheduledJobs(cfg.get(), ibValue(misspelt)), ibBackendException)
		<< "a filter field that names nothing must not answer with every job";
}

TEST_F(ScheduledJobFix, WhatAPredefinedJobCannotDoIsRefusedByName)
{
	if (!ready) GTEST_SKIP();

	EXPECT_THROW(ibValueScheduledJob::CreateScheduledJob(cfg.get(), ibValue(cleanup)), ibBackendException)
		<< "a predefined job exists once";
	EXPECT_THROW(ibValueScheduledJob::FindPredefined(cfg.get(), ibValue(ibString(wxT("NoSuchJob")))), ibBackendException);

	ibValue job = Find(cleanup);
	EXPECT_THROW(Call(job, wxT("Delete")), ibBackendException) << "it is declared, not stored";
	EXPECT_THROW(SetProp(job, wxT("Key"), ibValue(ibString(wxT("other")))), ibBackendException);
	EXPECT_THROW(SetProp(job, wxT("Description"), ibValue(ibString(wxT("other")))), ibBackendException);

	ibValuePtr<ibValueStructure> parameters(new ibValueStructure());
	parameters->Insert(ibValue(ibString(wxT("Node"))), ibValue(1));
	EXPECT_THROW(SetProp(job, wxT("Parameters"), ibValue(parameters)), ibBackendException)
		<< "a predefined job takes no parameters";
	EXPECT_THROW(SetProp(job, wxT("Schedule"), ibValue(600)), ibBackendException)
		<< "a schedule is a JobSchedule, not a number of seconds";

	// …and found by name, as well as by the metaobject.
	ibValue byName = ibValueScheduledJob::FindPredefined(cfg.get(), ibValue(ibString(wxT("Cleanup"))));
	EXPECT_EQ(Prop(byName, wxT("Key")).GetString(), ibString(wxT("Cleanup")));
}
