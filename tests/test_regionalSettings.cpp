////////////////////////////////////////////////////////////////////////////
//	Description : the base's regional settings - kept in the base, applied to its connections
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include <cstdlib>   // std::llabs
#include <memory>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionHolder.h"   // ibDatabaseConnectionHolder - a second holder for a second hand-out
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"    // ibConnectionScope - the door a pooled connection is handed out through
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/session/regionalSettings.h"
#include "backend/session/serverClock.h"

namespace {

// A SQLite base that SAYS it has a session zone - what Firebird and PostgreSQL are to the pool and to
// the clock, with no server to need: it takes the one name it knows and refuses every other, counts
// the statements it took, and its clones are of its own kind (the pool hands out clones).
class ZonedFakeBase : public ibDatabaseLayerSQLite {
public:
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
		clone->Open(wxT(":memory:"));
		return clone;
	}
	// What a Firebird reconnect does to a connection: the zone is gone from the session, and the name with it.
	void ForgetZone() { m_sessionTimeZone.clear(); }
};
int ZonedFakeBase::s_asked = 0;
int ZonedFakeBase::s_taken = 0;

// ...and one whose clock stands elsewhere, so that a measurement dropped shows as a difference gone.
class FrozenZonedBase : public ZonedFakeBase {
public:
	static ibDialectDictionary s_dialect;
	const ibDialectDictionary& GetDialect() const override { return s_dialect; }
};
ibDialectDictionary FrozenZonedBase::s_dialect = [] {
	ibDialectDictionary d = ibDatabaseLayerSQLite::Dialect();
	d.m_localTimestamp = wxT("'2030-01-01 12:00:00'");
	return d;
}();

// An application environment over an in-memory SQLite base, as test_tempDbSqlite brings one up.
struct RegionalFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	virtual std::shared_ptr<ibDatabaseLayerSQLite> MakeBase() { return std::make_shared<ibDatabaseLayerSQLite>(); }
	virtual std::size_t PoolSize() { return 1; }
	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = MakeBase();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, PoolSize(), /*minIdle=*/0);
		ibApplicationData::CreateTableSettings();
	}
	void TearDown() override {
		ibRegionalSettings::Reset();
		ibServerClock::Reset();
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};

// The same environment over the base that has a session zone, with room for a second connection.
struct ZonedRegionalFix : RegionalFix {
	std::shared_ptr<ibDatabaseLayerSQLite> MakeBase() override { return std::make_shared<ZonedFakeBase>(); }
	std::size_t PoolSize() override { return 2; }
};

} // namespace

// What is saved is what is read back, by every client of the base: one row, no user in the key.
TEST_F(RegionalFix, TheSettingsRoundTripThroughTheBase)
{
	EXPECT_FALSE(ibRegionalSettings::ApplyFromBase()) << "a base that names nothing";
	EXPECT_TRUE(ibRegionalSettings::Load().m_timeZone.IsEmpty());

	ibRegionalSettings settings;
	settings.m_timeZone = wxT("Europe/Kyiv");
	settings.m_locale = wxT("uk-UA");
	ASSERT_TRUE(ibRegionalSettings::Save(settings));

	const ibRegionalSettings read = ibRegionalSettings::Load();
	EXPECT_EQ(wxT("Europe/Kyiv"), read.m_timeZone);
	EXPECT_EQ(wxT("uk-UA"), read.m_locale);
	EXPECT_EQ(wxT("uk-UA"), ibRegionalSettings::Current().m_locale);
	EXPECT_EQ(wxT("uk-UA"), ibRegionalSettings::EffectiveLocale());

	// Read again as the start of a process reads it - and as every other process reads it once a
	// minute, which is how a zone saved here reaches a client that was already running.
	ibRegionalSettings::Reset();
	EXPECT_TRUE(ibRegionalSettings::Current().m_timeZone.IsEmpty());
	EXPECT_TRUE(ibRegionalSettings::ApplyFromBase());
	EXPECT_EQ(wxT("Europe/Kyiv"), ibRegionalSettings::Current().m_timeZone);
}

// SQLite has no session zone: a save keeps the name and puts nothing on, the connection says so,
// and the server lists no zones.
TEST_F(RegionalFix, ABaseWithoutASessionZoneSaysSo)
{
	EXPECT_FALSE(db->HasSessionTimeZone());
	EXPECT_FALSE(db->SetSessionTimeZone(wxT("Europe/Kyiv")));
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty());
	ibRegionalSettings settings;
	settings.m_timeZone = wxT("Europe/Kyiv");
	EXPECT_TRUE(ibRegionalSettings::Save(settings));
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty()) << "nothing to put it on";
	EXPECT_TRUE(ibRegionalSettings::KnownTimeZones().empty());
}

// The locale in force falls back from the base's to the platform's default.
TEST_F(RegionalFix, TheEffectiveLocaleFallsBackToThePlatform)
{
	ibRegionalSettings::Reset();
	const wxString platform = ibApplicationData::Get() != nullptr ? ibApplicationData::Get()->GetPlatformLocale() : wxString();
	EXPECT_EQ(platform, ibRegionalSettings::EffectiveLocale());
	ibRegionalSettings settings;
	settings.m_locale = wxT("de-DE");
	ASSERT_TRUE(ibRegionalSettings::Save(settings));
	EXPECT_EQ(wxT("de-DE"), ibRegionalSettings::EffectiveLocale());
}

// The clock is measured in the base's zone or not at all (serverClock.h): on a driver with a session
// zone nothing is measured until a zone is named, a name the server refuses measures nothing and
// leaves the connection as it was, and a name it takes puts the connection into it FIRST - which is
// how a connection bound before the setting comes to stand in the zone.
TEST_F(ZonedRegionalFix, TheClockIsMeasuredOnlyThroughAConnectionInTheBasesZone)
{
	EXPECT_FALSE(ibServerClock::Refresh(*db, wxEmptyString)) << "no zone named";
	EXPECT_EQ(0, ibServerClock::Offset());
	EXPECT_FALSE(ibServerClock::Refresh(*db, wxT("Nowhere/Nowhere"))) << "a name the server refuses";
	EXPECT_EQ(0, ibServerClock::Offset());
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty());

	const int before = ZonedFakeBase::s_taken;
	EXPECT_TRUE(ibServerClock::Refresh(*db, wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), db->GetSessionTimeZone()) << "put into the zone by the measurement";
	EXPECT_EQ(before + 1, ZonedFakeBase::s_taken);
	EXPECT_LT(std::llabs(static_cast<long long>(ibServerClock::Offset())), 5000) << "SQLite's clock is this machine's";
	// Standing in the zone already, the next measurement puts nothing on.
	EXPECT_TRUE(ibServerClock::Refresh(*db, wxT("Europe/Kyiv")));
	EXPECT_EQ(before + 1, ZonedFakeBase::s_taken);
}

// A difference measured in a zone is dropped the moment there is no zone to measure in, or the server
// refuses the one named: "now" is the machine's clock again, not the last zone's.
TEST(ServerClockZone, ClearingTheZoneReturnsNowToTheMachine)
{
	wxInitializer wx;
	FrozenZonedBase base;
	ASSERT_TRUE(base.Open(wxT(":memory:")));
	ASSERT_TRUE(ibServerClock::Refresh(base, wxT("Europe/Kyiv")));
	ASSERT_NE(0, ibServerClock::Offset()) << "the frozen clock stands years from this machine's";
	EXPECT_FALSE(ibServerClock::Refresh(base, wxEmptyString));
	EXPECT_EQ(0, ibServerClock::Offset());
	ASSERT_TRUE(ibServerClock::Refresh(base, wxT("Europe/Kyiv")));
	ASSERT_NE(0, ibServerClock::Offset());
	EXPECT_FALSE(ibServerClock::Refresh(base, wxT("Nowhere/Nowhere")));
	EXPECT_EQ(0, ibServerClock::Offset());
	ibServerClock::Reset();
}

// A save puts the zone on this thread's connection FIRST: a name the server refuses is refused there,
// with the reason, and nothing is written; a name it takes is written, in force on this connection, and
// every connection the pool hands out from then on - a second one to a second holder here, which is a
// clone - stands in it.
TEST_F(ZonedRegionalFix, ASaveIsRefusedByTheServerBeforeAnythingIsWritten)
{
	ibRegionalSettings refused;
	refused.m_timeZone = wxT("Nowhere/Nowhere");
	wxString why;
	EXPECT_FALSE(ibRegionalSettings::Save(refused, &why));
	EXPECT_TRUE(why.Contains(wxT("Nowhere/Nowhere"))) << why;
	EXPECT_TRUE(ibRegionalSettings::Load().m_timeZone.IsEmpty()) << "nothing written";
	EXPECT_TRUE(ibRegionalSettings::Current().m_timeZone.IsEmpty());
	EXPECT_TRUE(db->GetSessionTimeZone().IsEmpty());

	ibRegionalSettings taken;
	taken.m_timeZone = wxT("Europe/Kyiv");
	ASSERT_TRUE(ibRegionalSettings::Save(taken, &why)) << why;
	EXPECT_EQ(wxT("Europe/Kyiv"), ibRegionalSettings::Load().m_timeZone);
	EXPECT_EQ(wxT("Europe/Kyiv"), ibRegionalSettings::Current().m_timeZone);
	EXPECT_EQ(wxT("Europe/Kyiv"), db->GetSessionTimeZone()) << "the save's own connection";

	const ibConnectionScope first;   // this thread's: the base itself, in the zone already
	ASSERT_TRUE(static_cast<bool>(first));
	ibDatabaseConnectionHolder other;
	const ibConnectionScope second(&other);   // a second holder: a clone, put into the zone as it is handed out
	ASSERT_TRUE(static_cast<bool>(second));
	ASSERT_NE(first.get(), second.get());
	EXPECT_EQ(wxT("Europe/Kyiv"), second->GetSessionTimeZone());
}

// A hand-out the server would not put into the zone goes out as it is and is asked again at its next
// hand-out; one standing in the zone is not asked; one that lost its zone (a reconnect) is put back.
TEST_F(ZonedRegionalFix, AHandOutIsPutIntoTheZoneItIsNotIn)
{
	ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
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
		EXPECT_EQ(asked + 2, ZonedFakeBase::s_asked) << "asked again at the next hand-out, not marked done";
		EXPECT_TRUE(handed->GetSessionTimeZone().IsEmpty());
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
}

// A read of the row that FAILED is not "the base names nothing": what is in force stays - the zone,
// the connection's zone, the clock - and the next minute asks again. (Taking the failure for an empty
// row would put this process on the machine's clock, an hour from its peers where the machine stands
// in another zone, and the peers would sweep its sessions.)
TEST_F(ZonedRegionalFix, AFailedReadOfTheRowKeepsWhatIsInForce)
{
	ibRegionalSettings taken;
	taken.m_timeZone = wxT("Europe/Kyiv");
	ASSERT_TRUE(ibRegionalSettings::Save(taken));
	EXPECT_EQ(wxT("Europe/Kyiv"), ibRegionalSettings::Current().m_timeZone);
	EXPECT_EQ(wxT("Europe/Kyiv"), db->GetSessionTimeZone());

	db->RunQuery(wxT("DROP TABLE %s"), settings_table);   // the base is gone for a moment: every read of the row fails
	EXPECT_FALSE(ibRegionalSettings::ApplyFromBase());
	EXPECT_EQ(wxT("Europe/Kyiv"), ibRegionalSettings::Current().m_timeZone) << "kept";
	EXPECT_EQ(wxT("Europe/Kyiv"), db->GetSessionTimeZone()) << "kept";
}
