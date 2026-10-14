////////////////////////////////////////////////////////////////////////////
//	Description : the base's time zone - kept in the base, put on its connections (regionalSettings.h)
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/uilocale.h>

#include <chrono>
#include <cstdlib>   // std::llabs
#include <memory>
#include <thread>

#include "backend/appData.h"
#include "backend/backend_exception.h"                  // ibCoreException - why an opening was refused
#include "backend/databaseLayer/connectionHolder.h"       // ibDatabaseConnectionHolder - a second holder for a second hand-out
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"        // ibConnectionScope - the door a pooled connection is handed out through
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibCreateTable - the settings table, as the base makes it
#include "backend/appHost.h"                         // SetThreadOwner - a base's service thread
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/session/regionalSettings.h"

namespace {

// A SQLite base that SAYS it has a session zone - what Firebird and PostgreSQL are to the pool, with no
// server to need: it takes the one name it knows and refuses every other, counts the statements it was
// asked and took, and its clones are of its own kind, on the same file (the pool hands out clones; an
// in-memory SQLite connection would be a base of its own each time).
class ZonedFakeBase : public ibDatabaseLayerSQLite {
public:
	static wxString s_path;
	static int s_asked;   // zones the "server" was asked to take, across every instance...
	static int s_taken;   // ...and the ones it took
	bool HasSessionTimeZone() const override { return true; }
	bool SetSessionTimeZone(const wxString& zone) override {
		++s_asked;
		if (!zone.IsEmpty() && zone != wxT("Europe/Kyiv"))
			return false;
		m_sessionTimeZone = zone;
		++s_taken;
		return true;
	}
	ibDatabaseLayer* Clone() override {
		ZonedFakeBase* clone = new ZonedFakeBase();
		clone->Open(s_path);
		return clone;
	}
	// What a Firebird reconnect does to a connection: the zone is gone from the session, and the name with it.
	void ForgetZone() { m_sessionTimeZone.clear(); }
};
wxString ZonedFakeBase::s_path;
int ZonedFakeBase::s_asked = 0;
int ZonedFakeBase::s_taken = 0;

// ...and one whose server's clock stands elsewhere - years from this machine's - so that the distance
// measured, and a measurement dropped, are plain to see.
class FrozenZonedBase : public ZonedFakeBase {
public:
	static ibDialectDictionary s_dialect;
	const ibDialectDictionary& GetDialect() const override { return s_dialect; }
	ibDatabaseLayer* Clone() override {
		FrozenZonedBase* clone = new FrozenZonedBase();
		clone->Open(s_path);
		return clone;
	}
};
ibDialectDictionary FrozenZonedBase::s_dialect = [] {
	ibDialectDictionary d = ibDatabaseLayerSQLite::Dialect();
	d.m_localTimestamp = wxT("'2030-01-01 12:00:00'");
	return d;
}();
const ibDateTime kFrozen(2030, 1, 1, 12);

// This machine's own clock, and its clock in UTC - the oracles, outside the measurement.
ibDateTime MachineNow() { return ibDateTime::OfWxDateTime(wxDateTime::Now()); }
ibDateTime MachineUtc() {
	const wxDateTime::Tm tm = wxDateTime::Now().GetTm(wxDateTime::UTC);
	return ibDateTime(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
}

// A base over a SQLite file of its own, with sys_settings made as the base makes it.
struct RegionalFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayer>       db;
	wxString                               dbPath;
	ibRegionalSettings*                    regional = nullptr;
	virtual std::shared_ptr<ibDatabaseLayer> MakeBase() { return std::make_shared<ibDatabaseLayerSQLite>(); }
	virtual const wxChar* Extension() { return wxT("db"); }
	virtual std::size_t PoolSize() { return 1; }
	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		ASSERT_NE(pool, nullptr);
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		dbPath = wxFileName(wxStandardPaths::Get().GetTempDir(), wxString::Format(wxT("oes_regional_%s_%lld.%s"),
			::testing::UnitTest::GetInstance()->current_test_info()->name(), (long long)now, Extension())).GetFullPath();
		ZonedFakeBase::s_path = dbPath;
		try {
			db = MakeBase();
			if (db && !db->Open(dbPath))
				db.reset();
		}
		catch (...) { db.reset(); }
		if (!db)
			GTEST_SKIP() << "the base's driver is not on this machine (" << dbPath.ToStdString() << ")";
		pool->Init(db, PoolSize(), /*minIdle=*/0);
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(settings_table, {
			{ wxT("entryKey"),   ibTypeString(64),  false, true,  wxEmptyString },
			{ wxT("category"),   ibTypeInteger(),   true,  false, wxEmptyString },
			{ wxT("objectKey"),  ibTypeString(128), true,  false, wxEmptyString },
			{ wxT("settingKey"), ibTypeString(128), true,  false, wxT("''") },
			{ wxT("userKey"),    ibTypeString(36),  true,  false, wxT("''") },
			{ wxT("changed"),    ibTypeDate(),      true,  false, wxEmptyString },
			{ wxT("dataSize"),   ibTypeInteger(),   true,  false, wxEmptyString },
			{ wxT("binaryData"), ibTypeBlob(),      true,  false, wxEmptyString },
		}));
		regional = ibApplicationInstance::GetRegionalSettings();
		ASSERT_NE(regional, nullptr);
	}
	void TearDown() override {
		regional = nullptr;
		if (ibApplicationInstance::Get(false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (db) db->Close();
		db.reset();
		for (const wxChar* suffix : { wxT(""), wxT("-wal"), wxT("-shm"), wxT("-journal") })
			if (!dbPath.IsEmpty() && wxFileExists(dbPath + suffix))
				wxRemoveFile(dbPath + suffix);
	}
};

// The same base with a session zone, and room for a second connection.
struct ZonedRegionalFix : RegionalFix {
	std::shared_ptr<ibDatabaseLayer> MakeBase() override { return std::make_shared<ZonedFakeBase>(); }
	std::size_t PoolSize() override { return 2; }
};

// ...with a server's clock years away.
struct FrozenRegionalFix : RegionalFix {
	std::shared_ptr<ibDatabaseLayer> MakeBase() override { return std::make_shared<FrozenZonedBase>(); }
	std::size_t PoolSize() override { return 2; }
};

// ...and a real Firebird base, where the kit is beside the binary (CI's Firebird job); skips elsewhere.
struct FirebirdRegionalFix : RegionalFix {
	std::shared_ptr<ibDatabaseLayer> MakeBase() override {
		auto layer = std::make_shared<ibDatabaseLayerFirebird>();
		layer->SetUser(wxT("SYSDBA"));
		layer->SetPassword(wxT("masterkey"));
		return layer;
	}
	const wxChar* Extension() override { return wxT("fdb"); }
	std::size_t PoolSize() override { return 2; }
};

} // namespace

// What is saved is what is read back, by every client of the base: one row, no user in the key - and
// read again as every other process reads it once a minute, which is how a zone named here reaches a
// client that was already running.
TEST_F(RegionalFix, TheZoneRoundTripsThroughTheBase)
{
	EXPECT_FALSE(regional->Refresh()) << "a base that names nothing";
	EXPECT_TRUE(regional->GetTimeZone().IsEmpty());

	wxString why;
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv"), &why)) << why;
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());

	// Another process clears it: this one's next read takes the zone away too.
	ASSERT_TRUE(regional->SetTimeZone(wxEmptyString));
	EXPECT_TRUE(regional->Refresh());
	EXPECT_TRUE(regional->GetTimeZone().IsEmpty());
}

// SQLite has no session zone: the name is kept and put on nothing, the connection says so, and the
// server lists no zones.
TEST_F(RegionalFix, ABaseWithoutASessionZoneSaysSo)
{
	EXPECT_FALSE(db->HasSessionTimeZone());
	EXPECT_FALSE(db->SetSessionTimeZone(wxT("Europe/Kyiv")));
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty());
	EXPECT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty()) << "nothing to put it on";
	EXPECT_TRUE(regional->KnownTimeZones().empty());
}

// A name is put on a connection FIRST: one the server refuses is refused there, with the reason, and
// nothing is written; one it takes is written, and every connection the pool hands out from then on -
// a second one to a second holder here, which is a clone - stands in it.
TEST_F(ZonedRegionalFix, ANameIsRefusedByTheServerBeforeAnythingIsWritten)
{
	wxString why;
	EXPECT_FALSE(regional->SetTimeZone(wxT("Nowhere/Nowhere"), &why));
	EXPECT_TRUE(why.Contains(wxT("Nowhere/Nowhere"))) << why;
	EXPECT_TRUE(regional->GetTimeZone().IsEmpty());
	EXPECT_FALSE(regional->Refresh()) << "nothing written";

	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv"), &why)) << why;
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());

	const ibConnectionScope first;   // this thread's
	ASSERT_TRUE(static_cast<bool>(first));
	ibSingleConnectionHolder other;
	const ibConnectionScope second(&other);   // a second holder: a second connection, put into the zone as it is handed out
	ASSERT_TRUE(static_cast<bool>(second));
	ASSERT_NE(first.get(), second.get());
	EXPECT_EQ(wxT("Europe/Kyiv"), first->GetSessionTimeZone());
	EXPECT_EQ(wxT("Europe/Kyiv"), second->GetSessionTimeZone());
}

// A hand-out the server would not put into the zone goes out as it is, and the name is not tried on the
// hand-outs that follow until the zone is recorded again (once a minute, by the base's re-read); one standing
// in the zone is not asked; one that lost its zone (a reconnect) is put back.
TEST_F(ZonedRegionalFix, AHandOutIsPutIntoTheZoneItIsNotIn)
{
	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->SetSessionTimeZone(wxT("Nowhere/Nowhere")));   // recorded: the pool does not ask the server
	const int asked = ZonedFakeBase::s_asked;
	{
		const ibConnectionScope handed;   // an outermost scope is a hand-out; its end returns the connection
		ASSERT_TRUE(static_cast<bool>(handed));
		EXPECT_TRUE(handed->GetSessionTimeZone().IsEmpty()) << "refused, and not pretended";
		EXPECT_EQ(asked + 1, ZonedFakeBase::s_asked);
	}
	{
		const ibConnectionScope handed;
		ASSERT_TRUE(static_cast<bool>(handed));
		EXPECT_EQ(asked + 1, ZonedFakeBase::s_asked) << "a refused name is not tried on every hand-out";
		EXPECT_TRUE(handed->GetSessionTimeZone().IsEmpty());
	}
	ASSERT_TRUE(pool->SetSessionTimeZone(wxT("Nowhere/Nowhere")));   // recorded again: tried again
	{
		const ibConnectionScope handed;
		ASSERT_TRUE(static_cast<bool>(handed));
		EXPECT_EQ(asked + 2, ZonedFakeBase::s_asked);
	}
	ASSERT_TRUE(pool->SetSessionTimeZone(wxT("Europe/Kyiv")));
	const int before = ZonedFakeBase::s_taken;
	{
		const ibConnectionScope handed;
		ASSERT_TRUE(static_cast<bool>(handed));
		EXPECT_EQ(wxT("Europe/Kyiv"), handed->GetSessionTimeZone());
		EXPECT_EQ(before + 1, ZonedFakeBase::s_taken);
	}
	{
		const ibConnectionScope again;
		ASSERT_TRUE(static_cast<bool>(again));
		EXPECT_EQ(wxT("Europe/Kyiv"), again->GetSessionTimeZone());
		EXPECT_EQ(before + 1, ZonedFakeBase::s_taken) << "in the zone already: not put again";
	}
	std::static_pointer_cast<ZonedFakeBase>(db)->ForgetZone();   // what a reconnect leaves behind
	{
		const ibConnectionScope back;
		ASSERT_TRUE(static_cast<bool>(back));
		EXPECT_EQ(wxT("Europe/Kyiv"), back->GetSessionTimeZone()) << "put back at the next hand-out";
		EXPECT_EQ(before + 2, ZonedFakeBase::s_taken);
	}
	// The zone cleared: a connection standing in the old one is put back into the engine's own as it is handed out.
	ASSERT_TRUE(pool->SetSessionTimeZone(wxEmptyString));
	{
		const ibConnectionScope cleared;
		ASSERT_TRUE(static_cast<bool>(cleared));
		EXPECT_TRUE(cleared->GetSessionTimeZone().IsEmpty()) << "out of the zone the base no longer names";
		EXPECT_EQ(before + 3, ZonedFakeBase::s_taken);
	}
}

// A refresh through a holder that keeps its connection - the session registry's, bound long before the
// zone was named - puts THAT connection into the zone: no hand-out ever will.
TEST_F(ZonedRegionalFix, ARefreshPutsTheCallersOwnConnectionIntoTheZone)
{
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv")));
	ibSingleConnectionHolder kept;
	const ibConnectionScope held(&kept);
	ASSERT_TRUE(static_cast<bool>(held));
	std::static_pointer_cast<ZonedFakeBase>(held.shared())->ForgetZone();   // as a connection bound before the zone was named
	EXPECT_TRUE(held->GetSessionTimeZone().IsEmpty());
	EXPECT_TRUE(regional->Refresh(&kept));
	EXPECT_EQ(wxT("Europe/Kyiv"), held->GetSessionTimeZone());
}

// A read of the row that FAILED is not "the base names nothing": the zone in force stays, and the next
// minute asks again. (Taking the failure for an empty row would take every connection out of the base's
// zone on a hiccup.)
TEST_F(ZonedRegionalFix, AFailedReadOfTheRowKeepsWhatIsInForce)
{
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());

	{
		const ibConnectionScope scope;
		ASSERT_TRUE(static_cast<bool>(scope));
		scope->RunQuery(wxT("DROP TABLE %s"), settings_table);   // the base is gone for a moment: every read of the row fails
	}
	EXPECT_FALSE(regional->Refresh());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone()) << "kept";
}

// "NOW" IS THE BASE'S CLOCK on every thread that works for the base - a thread bound to it, and a thread its
// own service runs on (the registry's, the job tick's) - and the machine's on a thread that works for none.
TEST_F(FrozenRegionalFix, NowIsTheBasesClockOnEveryThreadThatWorksForIt)
{
	ibApplicationInstance* const base = ibApplicationInstance::Get();
	ASSERT_NE(base, nullptr);
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv")));
	EXPECT_LT(std::llabs(MachineNow().AddMilliseconds(ibApplicationInstance::Get()->GetClockOffset()) - kFrozen), 5000);

	{
		const ibApplicationInstanceScope bound(base);
		EXPECT_LT(std::llabs(ibDateTime::Now() - kFrozen), 5000) << "a thread bound to the base";
	}
	ibDateTime service, none;
	std::thread([&] {
		ibApplicationHost::SetThreadOwner(base);
		service = ibDateTime::Now();
		ibApplicationHost::SetThreadOwner(nullptr);
	}).join();
	std::thread([&] { none = ibDateTime::Now(); }).join();
	EXPECT_LT(std::llabs(service - kFrozen), 5000) << "the base's own service";
	EXPECT_LT(std::llabs(none - MachineNow()), 5000) << "a thread that works for no base";
}

// Measured in the base's zone or not at all: on a driver with a session zone nothing is measured until a
// zone is named, and a zone taken away takes the measurement with it - "now" is the machine's clock again,
// not the last zone's.
TEST_F(FrozenRegionalFix, TheClockIsMeasuredOnlyInAZoneTheBaseNames)
{
	regional->Refresh();
	EXPECT_EQ(0, ibApplicationInstance::Get()->GetClockOffset()) << "no zone named";
	wxString why;
	EXPECT_FALSE(regional->SetTimeZone(wxT("Nowhere/Nowhere"), &why));
	EXPECT_EQ(0, ibApplicationInstance::Get()->GetClockOffset()) << "a zone the server refuses";

	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv"), &why)) << why;
	EXPECT_NE(0, ibApplicationInstance::Get()->GetClockOffset());
	EXPECT_TRUE(regional->Refresh());
	EXPECT_LT(std::llabs(MachineNow().AddMilliseconds(ibApplicationInstance::Get()->GetClockOffset()) - kFrozen), 5000) << "measured again, the same";

	ASSERT_TRUE(regional->SetTimeZone(wxEmptyString));
	EXPECT_EQ(0, ibApplicationInstance::Get()->GetClockOffset()) << "no zone any more";
}

// A real server: Kyiv's clock is two or three hours ahead of UTC, wherever this machine stands - and "now"
// on a thread working for the base reads it.
TEST_F(FirebirdRegionalFix, NowIsKyivsClockOnceTheBaseNamesKyiv)
{
	wxString why;
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv"), &why)) << why;
	const ibApplicationInstanceScope bound(ibApplicationInstance::Get());
	const long long apart = ibDateTime::Now() - MachineUtc();
	EXPECT_TRUE(std::llabs(apart - 7200000) < 5000 || std::llabs(apart - 10800000) < 5000) << "Kyiv is UTC+2 or UTC+3, read " << apart;
}

// The locale stands beside the zone, each in a row of its own: named under the canonical name with its
// region, read back by every process that opens the base, answered by the base as its own ahead of the
// process's - and neither setting loses the other when one of them is written.
TEST_F(RegionalFix, TheLocaleRoundTripsThroughTheBaseBesideTheZone)
{
	ibApplicationInstance* const base = ibApplicationInstance::Get();
	ASSERT_NE(base, nullptr);
	const ibApplicationHost* const host = ibApplicationHost::Get();
	ASSERT_NE(host, nullptr);
	const wxLanguageInfo* const uk = wxUILocale::FindLanguageInfo(wxT("uk"));
	const wxLanguageInfo* const ru = wxUILocale::FindLanguageInfo(wxT("ru_RU"));
	ASSERT_NE(uk, nullptr);
	ASSERT_NE(ru, nullptr);
	const wxString ukraine = uk->GetCanonicalWithRegion();
	const wxString russia  = ru->GetCanonicalWithRegion();
	EXPECT_EQ(wxT("uk_UA"), ukraine);

	EXPECT_TRUE(regional->GetLocale().IsEmpty());
	EXPECT_EQ(host->GetLocale(), base->GetLocale()) << "a base that names none answers the process's";

	// Four spellings of one language, kept under the name the process will give of itself.
	wxString why;
	for (const wxChar* spelling : { wxT("uk"), wxT("uk_UA"), wxT("uk-UA"), wxT("Ukrainian") }) {
		ASSERT_TRUE(regional->SetLocale(spelling, &why)) << spelling << ": " << why;
		EXPECT_EQ(ukraine, regional->GetLocale()) << spelling;
	}
	EXPECT_EQ(ukraine, base->GetLocale());
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(ukraine, regional->GetLocale());

	// The zone written after it keeps it; it written after the zone keeps the zone.
	ASSERT_TRUE(regional->SetTimeZone(wxT("Europe/Kyiv"), &why)) << why;
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(ukraine, regional->GetLocale());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());
	ASSERT_TRUE(regional->SetLocale(wxT("ru_RU"), &why)) << why;
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(russia, regional->GetLocale());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());

	// Cleared: the process's again, and the zone still there.
	ASSERT_TRUE(regional->SetLocale(wxEmptyString, &why)) << why;
	EXPECT_TRUE(regional->Refresh());
	EXPECT_TRUE(regional->GetLocale().IsEmpty());
	EXPECT_EQ(host->GetLocale(), base->GetLocale());
	EXPECT_EQ(wxT("Europe/Kyiv"), regional->GetTimeZone());
}

// A name wx knows no language for is refused with the name in the reason, and nothing is written: the
// row keeps what it had.
TEST_F(RegionalFix, ALocaleNobodyKnowsIsRefusedBeforeAnythingIsWritten)
{
	const wxLanguageInfo* const en = wxUILocale::FindLanguageInfo(wxT("en"));
	ASSERT_NE(en, nullptr);
	wxString why;
	ASSERT_TRUE(regional->SetLocale(wxT("en"), &why)) << why;
	EXPECT_FALSE(regional->SetLocale(wxT("xx_YY"), &why));
	EXPECT_TRUE(why.Contains(wxT("xx_YY"))) << why;
	EXPECT_EQ(en->GetCanonicalWithRegion(), regional->GetLocale());
	EXPECT_TRUE(regional->Refresh());
	EXPECT_EQ(en->GetCanonicalWithRegion(), regional->GetLocale()) << "the row kept what it had";
}

// The engine's own words follow the base's locale on every thread that works for it: _() goes through the
// engine's catalogs (core/localization/translations.h), the language read off the base; a thread that works
// for no base speaks the process's. The catalogs are the tree's own (lang/uk, lang/ru), found by the walk up
// from the executable - a build tree inside the repository.
TEST_F(RegionalFix, TheEnginesWordsSpeakTheBasesLanguage)
{
	const wxString processLanguage = ibTranslations::GetProcessLanguage();
	const wxString failedUk = wxString::FromUTF8(" \xD0\xBD\xD0\xB5 \xD0\xB2\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8F");   // " не вдалося"
	const wxString failedRu = wxString::FromUTF8(" \xD0\xBD\xD0\xB5 \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C");   // " не удалось"
	const ibTranslations* const uk = ibTranslations::ForLanguage(wxT("uk_UA"));
	ASSERT_NE(uk, nullptr);
	ASSERT_TRUE(uk->IsLoaded(wxT("open_es"))) << "lang/uk/open_es.mo was not found from the executable";

	wxString why;
	ASSERT_TRUE(regional->SetLocale(wxT("uk_UA"), &why)) << why;
	EXPECT_EQ(failedUk, _(" failed"));
	ASSERT_TRUE(regional->SetLocale(wxT("ru_RU"), &why)) << why;
	EXPECT_EQ(failedRu, _(" failed")) << "the base's locale changed: its threads follow at once";

	// A thread that works for no base speaks the process's language, whatever this base names.
	ibTranslations::SetProcessLanguage(wxT("uk_UA"));
	wxString elsewhere;
	std::thread([&elsewhere]() { elsewhere = _(" failed"); }).join();
	EXPECT_EQ(failedUk, elsewhere);
	EXPECT_EQ(failedRu, _(" failed"));

	// A base that names none: the process's.
	ASSERT_TRUE(regional->SetLocale(wxEmptyString, &why)) << why;
	EXPECT_EQ(failedUk, _(" failed"));
	ibTranslations::SetProcessLanguage(processLanguage);
}

// A base opened the way a program opens it comes up in the locale it names, ahead of the opener's word:
// the process's wxLocale is settled from the base's row. Runs where Firebird embedded is laid out beside
// the binary (CI's Firebird job); skips elsewhere.
TEST(FirebirdRegionalOpen, ABaseOpensInTheLocaleItNames)
{
	wxInitializer init;
	if (!init.IsOk())
		GTEST_SKIP() << "wxBase init failed";
	const wxLanguageInfo* const en = wxUILocale::FindLanguageInfo(wxT("en"));
	const wxLanguageInfo* const ru = wxUILocale::FindLanguageInfo(wxT("ru_RU"));
	ASSERT_NE(en, nullptr);
	ASSERT_NE(ru, nullptr);

	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const wxString folder = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes_regional_open_%lld"), (long long)now)).GetFullPath();
	ASSERT_TRUE(wxFileName::Mkdir(folder));
	struct Folder { wxString path; ~Folder() { wxFileName::Rmdir(path, wxPATH_RMDIR_RECURSIVE); } } cleanup{ folder };
	// Whatever stops this test, the base is closed with the process before the folder goes: declared after
	// the folder, so it runs first.
	struct Opened { ~Opened() { if (ibApplicationHost::Get() != nullptr) ibApplicationInstance::DestroyAppDataEnv(); } } closing;

	ibFileInstanceRequest opening;
	opening.m_directory = folder;
	opening.m_create    = true;
	opening.m_locale    = wxT("en");   // the opener's word: what a base that names none comes up in
	wxString reason;
	const auto open = [&opening, &reason]() -> ibApplicationInstance* {
		try {
			const ibApplicationInstanceScope scope(nullptr);
			return ibApplicationInstance::CreateAppDataEnv(opening);
		}
		catch (const ibCoreException& err) { reason = err.GetErrorDescription(); return nullptr; }
	};
	ibApplicationInstance* base = open();
	if (base == nullptr) {
		// A client that is here and still did not open the base is a failure, not a skip.
		ASSERT_FALSE(ibDatabaseLayerFirebird::IsAvailable()) << "the base did not open: " << reason.ToStdString();
		GTEST_SKIP() << "no Firebird client on this machine (" << folder.ToStdString() << ")";
	}
	if (wxUILocale::FromTag(en->GetCanonicalWithRegion()).IsSupported())
		EXPECT_EQ(en->GetCanonicalWithRegion(), ibApplicationHost::Get()->GetLocale()) << "a base that names none: the opener's word";
	ibRegionalSettings* regional = ibApplicationInstance::GetRegionalSettings(base);
	ASSERT_NE(regional, nullptr);
	wxString why;
	ASSERT_TRUE(regional->SetLocale(wxT("ru_RU"), &why)) << why;
	ASSERT_TRUE(ibApplicationInstance::DestroyAppDataEnv());   // every base and the process with it: wxLocale is settled again by the next opening

	// Opened again with the same word: the base's own name is ahead of it, and the base answers it as its own
	// whatever the process speaks. The process speaks it where this machine has the locale; where it has not
	// (a Linux with ru_RU not generated) the base still opens, in the machine's own, and the journal says so.
	base = open();
	ASSERT_NE(base, nullptr) << "a locale this machine cannot grant must not refuse the base: " << reason.ToStdString();
	EXPECT_EQ(ru->GetCanonicalWithRegion(), base->GetLocale());
	if (wxUILocale::FromTag(ru->GetCanonicalWithRegion()).IsSupported())
		EXPECT_EQ(ru->GetCanonicalWithRegion(), ibApplicationHost::Get()->GetLocale());
	ASSERT_TRUE(ibApplicationInstance::DestroyAppDataEnv());
}
