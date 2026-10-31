// =============================================================================
// ibCrashGuard::InstallStopSignals - the first SIGINT or SIGTERM marks the stop and wakes whoever asked, the second
// takes the default action: a program too busy to look at the stop can still be ended. Each case runs in a process of
// its own, because the fact it sets cannot be unset. Windows ends a window by WM_CLOSE, and raises these differently.
// =============================================================================

#include <gtest/gtest.h>

#ifndef _WIN32

#include "core/diagnostics/crashGuard.h"

#include <csignal>

#include <signal.h>
#include <unistd.h>

#ifdef __linux__
#include <wx/app.h>
#include <wx/init.h>
#endif

namespace {

volatile std::sig_atomic_t s_woken = 0;

void Wake(int signal)
{
	s_woken = signal;
}

void MarksAndWakesThenTheSecondEnds(int second)
{
	ibCrashGuard::InstallStopSignals(&Wake);
	if (ibCrashGuard::IsStopRequested())
		::_exit(1);
	std::raise(SIGTERM);
	if (!ibCrashGuard::IsStopRequested() || s_woken != SIGTERM)
		::_exit(2);
	std::raise(second);
	::_exit(3);
}

void MarksWithNobodyToWake()
{
	ibCrashGuard::InstallStopSignals();
	std::raise(SIGINT);
	::_exit(ibCrashGuard::IsStopRequested() ? 0 : 1);
}

#ifdef __linux__
void OnTheLoop(int)
{
	::_exit(0);
}

// What ibWxApp::OnInit does: wx's handler read back and made the wake, so the signal still reaches wx's CheckSignal.
void WxsHandlerIsTheWake()
{
	wxInitializer wx;
	wxAppConsole* const app = wxAppConsole::GetInstance();
	if (!wx.IsOk() || app == nullptr || !app->SetSignalHandler(SIGTERM, &OnTheLoop))
		::_exit(1);
	struct sigaction wxHandler;
	if (::sigaction(SIGTERM, nullptr, &wxHandler) != 0)
		::_exit(2);
	ibCrashGuard::InstallStopSignals(wxHandler.sa_handler);
	std::raise(SIGTERM);
	app->CheckSignal();
	::_exit(3);
}
#endif

} // namespace

TEST(StopSignals, TheFirstMarksAndWakesTheSecondOfAnyKindEnds)
{
	::testing::FLAGS_gtest_death_test_style = "threadsafe";
	EXPECT_EXIT(MarksAndWakesThenTheSecondEnds(SIGTERM), ::testing::KilledBySignal(SIGTERM), "");
	EXPECT_EXIT(MarksAndWakesThenTheSecondEnds(SIGINT), ::testing::KilledBySignal(SIGINT), "");
}

TEST(StopSignals, NobodyToWakeStillMarks)
{
	::testing::FLAGS_gtest_death_test_style = "threadsafe";
	EXPECT_EXIT(MarksWithNobodyToWake(), ::testing::ExitedWithCode(0), "");
}

#ifdef __linux__
TEST(StopSignals, TheMainLoopIsWokenThroughWxsOwnHandler)
{
	::testing::FLAGS_gtest_death_test_style = "threadsafe";
	EXPECT_EXIT(WxsHandlerIsTheWake(), ::testing::ExitedWithCode(0), "");
}
#endif

#endif // _WIN32
