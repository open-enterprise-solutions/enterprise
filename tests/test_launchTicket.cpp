#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/userInfo.h"
#include "core/launchTicket.h"

#if !defined(__WXMSW__)
#  include <signal.h>
#endif

namespace {

ibString OutPath(const ibString& name)
{
	return wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + name;
}

ibString ReadFile(const ibString& path)
{
	wxFile file;
	if (!file.Open(path, wxFile::read))
		return ibString();
	const wxFileOffset length = file.Length();
	if (length <= 0)
		return ibString();
	wxCharBuffer buf(static_cast<size_t>(length));
	const ssize_t got = file.Read(buf.data(), static_cast<size_t>(length));
	if (got <= 0)
		return ibString();
	return ibString::FromUTF8(buf.data(), static_cast<size_t>(got));
}

} // namespace

// The process a launch starts. It reads the password from its own stdin and
// writes that password where the parent asked. Skipped when this binary was
// not started as that child.
TEST(LaunchSecrets, ChildWritesWhatItRead)
{
	if (std::getenv("OES_LAUNCH_CHILD") == nullptr) {
		GTEST_SKIP();
		return;
	}
	ibLaunchSecrets got;
	ibApplyLaunchSecrets(got, false, false);
	ASSERT_FALSE(got.m_ibPassword.IsEmpty());
	const char* out = std::getenv("OES_LAUNCH_OUT");
	ASSERT_NE(out, nullptr);
	wxFile file;
	ASSERT_TRUE(file.Create(ibString::FromUTF8(out), true));
	const std::string utf8 = got.m_ibPassword.utf8_str();
	ASSERT_EQ(file.Write(utf8.data(), utf8.length()), static_cast<wxFileOffset>(utf8.length()));
}

#if !defined(__WXMSW__)

TEST(LaunchSecrets, AChildReadsThePasswordTwiceAndTheCommandLineDoesNotCarryIt)
{
	const ibString password = wxT("p@ss\nword\"oes");
	ibLaunchSecrets secrets;
	secrets.m_ibPassword = password;
	secrets.m_dbPassword = wxT("db-secret-oes-9f3a");

	const ibString exe = wxStandardPaths::Get().GetExecutablePath();
	const ibString cmd = wxT("\"") + exe + wxT("\" --gtest_filter=LaunchSecrets.ChildWritesWhatItRead");

	const ibString first = OutPath(wxT("oes-launch-1"));
	const ibString second = OutPath(wxT("oes-launch-2"));
	wxRemoveFile(first);
	wxRemoveFile(second);

	auto once = [&](const ibString& out) {
		ASSERT_EQ(setenv("OES_LAUNCH_CHILD", "1", 1), 0);
		ASSERT_EQ(setenv("OES_LAUNCH_OUT", out.ToUtf8().c_str(), 1), 0);
		const long pid = ibStartProcess(cmd, secrets);
		unsetenv("OES_LAUNCH_CHILD");
		unsetenv("OES_LAUNCH_OUT");
		ASSERT_GT(pid, 0);
		// ibStartProcess reaps its own child, so the test waits for what the child writes, not for the child.
		for (int i = 0; i < 100 && ReadFile(out).IsEmpty(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	};

	once(first);
	once(second);
	EXPECT_EQ(ReadFile(first), password);
	EXPECT_EQ(ReadFile(second), password);
	wxRemoveFile(first);
	wxRemoveFile(second);

	const long sleeper = ibStartProcess(wxT("sleep 30"), secrets);
	ASSERT_GT(sleeper, 0);
	const ibString cmdlinePath = ibString::Format(wxT("/proc/%ld/cmdline"), sleeper);
	const ibString cmdline = ReadFile(cmdlinePath);
	EXPECT_FALSE(cmdline.Contains(password));
	EXPECT_FALSE(cmdline.Contains(wxT("db-secret-oes-9f3a")));
	EXPECT_FALSE(cmdline.Contains(wxT("--password=")));
	kill(static_cast<pid_t>(sleeper), SIGTERM);
}

#endif

TEST(LaunchSecrets, ASecondLaunchKeepsTheSessionPassword)
{
	const bool mine = ibApplicationInstance::Get() == nullptr;
	if (mine && !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable";

	ibSession session(wxT("launch-keep"), ibSessionKind::Designer);
	ibSessionScope scope(&session);
	ibUserInfo info;
	info.m_strUserName = wxT("Ada");
	ibApplicationInstance::GetSessionRegistry()->InstallUser(&session, info, wxT("kept-secret"));
	ASSERT_EQ(session.GetSessionRawPassword(), wxT("kept-secret"));

	(void)ibApplicationInstance::Get()->RunApplication(wxT("oes-no-such-binary-387e"), false, false);
	EXPECT_EQ(session.GetSessionRawPassword(), wxT("kept-secret"));
	(void)ibApplicationInstance::Get()->RunApplication(wxT("oes-no-such-binary-387e"), false, false);
	EXPECT_EQ(session.GetSessionRawPassword(), wxT("kept-secret"));

	if (mine)
		ibApplicationInstance::DestroyAppDataEnv();
}
