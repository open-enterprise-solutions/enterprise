// =============================================================================
// OES Enterprise — ibJobManager unit tests
//
// Scope: everything the manager decides WITHOUT a live process behind it — the
// window arithmetic, the registration contract, and the refusal paths.
//
// Registering a job creates a session, which needs a running ibApplicationData
// (registry thread + connection pool + metadata). That is integration scope and
// is not covered here; what IS covered is that a manager with no application
// data behind it refuses cleanly instead of half-registering — the failure mode
// that would otherwise surface as a job silently never running.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/job/jobManager.h"
#include "backend/compiler/procUnitValues.h"   // ibValueIterator — a session-bound value
#include "backend/backend_exception.h"         // ibBackendException — what the gate throws

// ---------------------------------------------------------------------------
// Window arithmetic — pure, no clock. The wrap-past-midnight case is the whole
// reason this is a separate function: read as a plain range, a 22..5 window
// matches no hour at all, and the job it guards would never run.
// ---------------------------------------------------------------------------

namespace {
// Minutes-from-midnight, so the tests read as clock time.
constexpr int At(int hour, int minute = 0) { return hour * 60 + minute; }
}

TEST(JobSchedule, NoWindowIsAlwaysInside) {
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(-1, -1, At(0)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(-1, -1, At(13)));
    // One bound unset is still "no window" — a half-declared window is not a
    // window, and treating it as one would silently gate the job.
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(2), -1, At(20)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(-1, At(5), At(20)));
}

TEST(JobSchedule, DaytimeWindowIsHalfOpen) {
    // [10:00, 15:00): the start is in, the end is out — half-open so consecutive
    // windows cannot both claim the boundary minute.
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(10), At(15), At(10)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(10), At(15), At(14, 59)));
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(10), At(15), At(15)));
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(10), At(15), At(9, 59)));
}

TEST(JobSchedule, WindowResolvesMinutes) {
    // Minutes, not hours — "from 02:30" has to be expressible.
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(2, 30), At(5), At(2, 29)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(2, 30), At(5), At(2, 30)));
}

TEST(JobSchedule, NightWindowWrapsMidnight) {
    // [22:00, 05:00) — the night window a heavy housekeeping job declares. Read
    // as a plain range this would match no minute at all.
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(22), At(5), At(22)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(22), At(5), At(0)));
    EXPECT_TRUE(ibJobSchedule::IsInsideWindow(At(22), At(5), At(4, 59)));
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(22), At(5), At(5)));
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(22), At(5), At(12)));
}

TEST(JobSchedule, EqualBoundsMatchNothing) {
    // [3, 3) is empty, not "all day" — and IsValid refuses it, so a job declared
    // this way is rejected at registration rather than never running.
    EXPECT_FALSE(ibJobSchedule::IsInsideWindow(At(3), At(3), At(3)));

    ibJobSchedule s = ibJobSchedule::EverySeconds(60);
    s.m_startMinute = At(3);
    s.m_endMinute   = At(3);
    EXPECT_FALSE(s.IsValid());
}

// ---------------------------------------------------------------------------
// The calendar combines — "10:00-15:00 on Tuesdays in March" is an AND of
// independent fields, not a grammar
// ---------------------------------------------------------------------------

TEST(JobSchedule, DefaultAllowsAnyMoment) {
    const ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    EXPECT_TRUE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 3, 0)));
    EXPECT_TRUE(s.IsAllowed(wxDateTime(1, wxDateTime::Jan, 2026, 23, 59)));
}

TEST(JobSchedule, WeekDayNarrows) {
    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_daysOfWeek = ibJobWeekDay_Tuesday;

    EXPECT_TRUE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 12, 0)));   // Tuesday
    EXPECT_FALSE(s.IsAllowed(wxDateTime(18, wxDateTime::Mar, 2026, 12, 0)));  // Wednesday
}

TEST(JobSchedule, WindowWeekDayAndMonthCombine) {
    // The case in full: 10:00-15:00, Tuesdays, March only.
    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_startMinute = At(10);
    s.m_endMinute   = At(15);
    s.m_daysOfWeek  = ibJobWeekDay_Tuesday;
    s.m_months      = 1u << wxDateTime::Mar;

    EXPECT_TRUE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 12, 0)));

    EXPECT_FALSE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 9, 0)));   // too early
    EXPECT_FALSE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 15, 0)));  // end is exclusive
    EXPECT_FALSE(s.IsAllowed(wxDateTime(18, wxDateTime::Mar, 2026, 12, 0)));  // wrong weekday
    EXPECT_FALSE(s.IsAllowed(wxDateTime(21, wxDateTime::Apr, 2026, 12, 0)));  // right weekday, wrong month
}

TEST(JobSchedule, DayOfMonthNarrows) {
    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_daysOfMonth = 1u << 0;   // the 1st

    EXPECT_TRUE(s.IsAllowed(wxDateTime(1, wxDateTime::Mar, 2026, 12, 0)));
    EXPECT_FALSE(s.IsAllowed(wxDateTime(2, wxDateTime::Mar, 2026, 12, 0)));
}

TEST(JobSchedule, ValidityRangeBounds) {
    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_activeFrom = wxDateTime(1, wxDateTime::Mar, 2026);
    s.m_activeTo   = wxDateTime(31, wxDateTime::Mar, 2026);

    EXPECT_TRUE(s.IsAllowed(wxDateTime(17, wxDateTime::Mar, 2026, 12, 0)));
    EXPECT_FALSE(s.IsAllowed(wxDateTime(17, wxDateTime::Feb, 2026, 12, 0)));
    EXPECT_FALSE(s.IsAllowed(wxDateTime(17, wxDateTime::Apr, 2026, 12, 0)));

    // Inverted range names no moment at all — refused rather than silent.
    std::swap(s.m_activeFrom, s.m_activeTo);
    EXPECT_FALSE(s.IsValid());
}

TEST(JobSchedule, NextAllowedSkipsToTheWindow) {
    ibJobSchedule s = ibJobSchedule::Nightly(2, 5);

    // Asked at noon, the next allowed moment is 02:00 the following day.
    const wxDateTime next = s.NextAllowedAfter(wxDateTime(17, wxDateTime::Mar, 2026, 12, 0));
    ASSERT_TRUE(next.IsValid());
    EXPECT_EQ(18, next.GetDay());
    EXPECT_EQ(2,  next.GetHour());
    EXPECT_EQ(0,  next.GetMinute());
}

TEST(JobSchedule, NextAllowedSkipsToTheWeekday) {
    ibJobSchedule s = ibJobSchedule::EverySeconds(24 * 3600);
    s.m_daysOfWeek = ibJobWeekDay_Monday;

    // 17 Mar 2026 is a Tuesday; the next Monday is the 23rd.
    const wxDateTime next = s.NextAllowedAfter(wxDateTime(17, wxDateTime::Mar, 2026, 12, 0));
    ASSERT_TRUE(next.IsValid());
    EXPECT_EQ(23, next.GetDay());
}

TEST(JobSchedule, ImpossibleCalendarHasNoNextRun) {
    // 30 February: every field is individually legal, the combination is not.
    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_daysOfMonth = 1u << 29;             // the 30th
    s.m_months      = 1u << wxDateTime::Feb;

    EXPECT_FALSE(s.NextAllowedAfter(wxDateTime(1, wxDateTime::Jan, 2026)).IsValid());
}

TEST(JobSchedule, ToStringNamesOnlyWhatWasSet) {
    const wxString plain = ibJobSchedule::EverySeconds(600).ToString();
    EXPECT_FALSE(plain.IsEmpty());
    // Defaults are not restated — a description that lists "any day, any month"
    // is one nobody finishes reading.
    EXPECT_EQ(wxNOT_FOUND, plain.Find(wxT(",")));

    ibJobSchedule s = ibJobSchedule::EverySeconds(600);
    s.m_startMinute = At(10);
    s.m_endMinute   = At(15);
    EXPECT_NE(wxNOT_FOUND, s.ToString().Find(wxT("10:00-15:00")));
}

// ---------------------------------------------------------------------------
// The argument gate — plain values travel, session-bound ones do not.
//
// The rule itself lives on ibValue::IsTransferable(); what is checked here is
// that the job layer asks every element and reports the FIRST refusal, so a bad
// argument is named at submission instead of surfacing in the background where
// no caller is left to hear about it.
// ---------------------------------------------------------------------------

TEST(JobManager, EmptyArgumentArrayTravels) {
    std::vector<ibValue> args;
    EXPECT_EQ(-1, ibJobManager::FindNonTransferable(args));
}

TEST(JobManager, PlainValuesTravel) {
    std::vector<ibValue> args;
    args.push_back(ibValue(42));
    args.push_back(ibValue(wxT("text")));
    args.push_back(ibValue(true));
    args.push_back(ibValue());            // undefined
    EXPECT_EQ(-1, ibJobManager::FindNonTransferable(args));
}

TEST(JobManager, IteratorRefusesToTravel) {
    // A cursor is a position inside somebody else's collection — advancing it
    // from a second session would move it under the first.
    ibValueIterator cursor;
    EXPECT_FALSE(cursor.IsTransferable());
}

TEST(JobManager, CheckPassesOnPlainValues) {
    std::vector<ibValue> args;
    args.push_back(ibValue(1));
    args.push_back(ibValue(wxT("ok")));
    EXPECT_NO_THROW(ibJobManager::CheckTransferable(args));
}

TEST(JobManager, CheckThrowsOnMutableValue) {
    // The author has to SEE this, at the call, on their own stack — a job that
    // quietly never runs is the failure mode this exists to prevent.
    std::vector<ibValue> args;
    args.push_back(ibValue(1));
    args.push_back(ibValue(new ibValueIterator()));
    EXPECT_THROW(ibJobManager::CheckTransferable(args), ibBackendException);
}

// ---------------------------------------------------------------------------
// Empty manager — the contract before anything is registered
// ---------------------------------------------------------------------------

TEST(JobManager, StartsEmpty) {
    ibJobManager manager(ib::AppDataCtorToken{});
    EXPECT_EQ(0u, manager.Count());
    EXPECT_EQ(0u, manager.RunningCount());
}

TEST(JobManager, TickOnEmptyLaunchesNothing) {
    ibJobManager manager(ib::AppDataCtorToken{});
    EXPECT_EQ(0, manager.Tick());
}

TEST(JobManager, RunNowRejectsUnknownName) {
    ibJobManager manager(ib::AppDataCtorToken{});
    EXPECT_FALSE(manager.RunNow(wxT("nobody")));
}

TEST(JobManager, UnregisterRejectsUnknownName) {
    ibJobManager manager(ib::AppDataCtorToken{});
    EXPECT_FALSE(manager.Unregister(wxT("nobody")));
}

// ---------------------------------------------------------------------------
// Registration contract — rejections happen at Register, in front of the
// caller, never as silence at tick time
// ---------------------------------------------------------------------------

TEST(JobManager, RegisterRejectsEmptyName) {
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxEmptyString;
    desc.m_body = [](ibSession*) { return false; };
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(0u, manager.Count());
}

TEST(JobManager, RegisterRejectsMissingBody) {
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("bodyless");
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(0u, manager.Count());
}

TEST(JobManager, RegisterRejectsNonPositiveInterval) {
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("zero-interval");
    desc.m_body = [](ibSession*) { return false; };
    desc.m_schedule.m_intervalSeconds = 0;
    EXPECT_FALSE(manager.Register(desc));

    desc.m_schedule.m_intervalSeconds = -60;
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(0u, manager.Count());
}

TEST(JobManager, RegisterRejectsImpossibleSchedule) {
    // A schedule that can never match is refused at registration rather than
    // left to be debugged as silence — an empty window is the easy way to write
    // one by accident.
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("never-matches");
    desc.m_body = [](ibSession*) { return false; };
    desc.m_schedule = ibJobSchedule::EverySeconds(60);
    desc.m_schedule.m_startMinute = At(3);
    desc.m_schedule.m_endMinute   = At(3);
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(0u, manager.Count());
}

TEST(JobManager, RegisterNeedsNoApplicationData) {
    // Registration is DECLARATION ONLY — no session, no database, no metadata.
    // That is what lets the platform's list be declared the moment a database
    // opens, and it is why a job that never comes due costs nothing: the session
    // is built on first launch instead.
    //
    // There is no appData in a unit-test process, and it must not matter.
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("declared-only");
    desc.m_body = [](ibSession*) { return false; };
    EXPECT_TRUE(manager.Register(desc));
    EXPECT_EQ(1u, manager.Count());

    // And nothing ran: no session could be created, so Launch declines. The job
    // stays declared and will start the first time it can.
    EXPECT_EQ(0, manager.Tick());
    EXPECT_EQ(0u, manager.RunningCount());
}

TEST(JobManager, RegisterRejectsDuplicateName) {
    // A duplicate means a double bootstrap. Refusing makes it visible; quietly
    // keeping one copy would hide it until something ran twice.
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("twice");
    desc.m_body = [](ibSession*) { return false; };

    EXPECT_TRUE(manager.Register(desc));
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(1u, manager.Count());
}

TEST(JobManager, RegisterRefusesPastTheCap) {
    // Each job holds a session, and a session holds a connection out of a pool
    // whose Checkout blocks when exhausted — so the cap is a refusal at
    // registration, not a deferral discovered later as silence.
    ibJobManager manager(ib::AppDataCtorToken{});
    manager.SetMaxJobs(2);

    for (int i = 0; i < 2; ++i) {
        ibJobDescription desc;
        desc.m_name = wxString::Format(wxT("job-%d"), i);
        desc.m_body = [](ibSession*) { return false; };
        EXPECT_TRUE(manager.Register(desc));
    }

    ibJobDescription overflow;
    overflow.m_name = wxT("one-too-many");
    overflow.m_body = [](ibSession*) { return false; };
    EXPECT_FALSE(manager.Register(overflow));
    EXPECT_EQ(2u, manager.Count());
}

TEST(JobManager, UnregisterDropsTheJob) {
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name = wxT("temporary");
    desc.m_body = [](ibSession*) { return false; };

    ASSERT_TRUE(manager.Register(desc));
    EXPECT_TRUE(manager.Unregister(wxT("temporary")));
    EXPECT_EQ(0u, manager.Count());
    // Gone means gone — a second Unregister has nothing to find.
    EXPECT_FALSE(manager.Unregister(wxT("temporary")));
}

TEST(JobManager, SnapshotReportsDeclaredJobs) {
    ibJobManager manager(ib::AppDataCtorToken{});
    ibJobDescription desc;
    desc.m_name     = wxT("watched");
    desc.m_body     = [](ibSession*) { return false; };
    desc.m_schedule = ibJobSchedule::EverySeconds(600);
    ASSERT_TRUE(manager.Register(desc));

    const std::vector<ibJobState> snap = manager.Snapshot();
    ASSERT_EQ(1u, snap.size());
    EXPECT_EQ(wxT("watched"), snap[0].m_name);
    // Never ran yet: no outcome, no last run, and no promised next run — an empty
    // cell reads as "on the next tick" rather than as a missing value.
    EXPECT_EQ(ibJobOutcome::Never, snap[0].m_outcome);
    EXPECT_FALSE(snap[0].m_lastRunAt.IsValid());
    EXPECT_FALSE(snap[0].m_nextRunAt.IsValid());
    // The schedule reads back as a sentence, which is what a settings list shows.
    EXPECT_FALSE(snap[0].m_schedule.IsEmpty());
}

// ---------------------------------------------------------------------------
// Caps and shutdown
// ---------------------------------------------------------------------------

TEST(JobManager, MaxJobsRoundTrips) {
    ibJobManager manager(ib::AppDataCtorToken{});
    manager.SetMaxJobs(9);
    EXPECT_EQ(9u, manager.GetMaxJobs());
}

TEST(JobManager, StopIsIdempotent) {
    ibJobManager manager(ib::AppDataCtorToken{});
    manager.Stop();
    manager.Stop();
    EXPECT_EQ(0u, manager.Count());
}

// ---------------------------------------------------------------------------
// Background runs — the refusal paths reachable without a live process
// ---------------------------------------------------------------------------

TEST(JobManager, StartBackgroundRejectsEmptyName) {
    ibJobManager manager(ib::AppDataCtorToken{});
    EXPECT_THROW(manager.StartBackground(wxEmptyString), ibBackendException);
}

TEST(JobManager, StartBackgroundRejectsMutableArgumentBeforeAnythingElse) {
    // The gate runs FIRST — before the name check, before the session. A bad
    // argument must be reported even when everything after it would also have
    // failed, otherwise the author fixes the wrong thing.
    ibJobManager manager(ib::AppDataCtorToken{});
    std::vector<ibValue> args;
    args.push_back(ibValue(new ibValueIterator()));
    EXPECT_THROW(manager.StartBackground(wxEmptyString, args), ibBackendException);
}

TEST(JobManager, StoppedManagerAcceptsNothing) {
    ibJobManager manager(ib::AppDataCtorToken{});
    manager.Stop();

    ibJobDescription desc;
    desc.m_name = wxT("late");
    desc.m_body = [](ibSession*) { return false; };
    EXPECT_FALSE(manager.Register(desc));
    EXPECT_EQ(0, manager.Tick());
    EXPECT_FALSE(manager.RunNow(wxT("late")));
}
