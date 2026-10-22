#include <gtest/gtest.h>

#include <cstdlib>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/userInfo.h"
#include "core/launchTicket.h"

#if !defined(__WXMSW__)
#  include <signal.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace {

wxString OutPath(const wxString& name)
{
	return wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + name;
}

wxString ReadFile(const wxString& path)
{
	wxFile file;
	if (!file.Open(path, wxFile::read))
		return wxString();
	const wxFileOffset length = file.Length();
	if (length <= 0)
		return wxString();
	wxCharBuffer buf(static_cast<size_t>(length));
	const ssize_t got = file.Read(buf.data(), static_cast<size_t>(length));
	if (got <= 0)
		return wxString();
	return wxString::FromUTF8(buf.data(), static_cast<size_t>(got));
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
	ASSERT_TRUE(file.Create(wxString::FromUTF8(out), true));
	const wxScopedCharBuffer utf8 = got.m_ibPassword.utf8_str();
	ASSERT_EQ(file.Write(utf8.data(), utf8.length()), static_cast<wxFileOffset>(utf8.length()));
}

#if !defined(__WXMSW__)

TEST(LaunchSecrets, AChildReadsThePasswordTwiceAndTheCommandLineDoesNotCarryIt)
{
	const wxString password = wxT("p@ss\nword\"oes");
	ibLaunchSecrets secrets;
	secrets.m_ibPassword = password;
	secrets.m_dbPassword = wxT("db-secret-oes-9f3a");

	const wxString exe = wxStandardPaths::Get().GetExecutablePath();
	const wxString cmd = wxT("\"") + exe + wxT("\" --gtest_filter=LaunchSecrets.ChildWritesWhatItRead");

	const wxString first = OutPath(wxT("oes-launch-1"));
	const wxString second = OutPath(wxT("oes-launch-2"));
	wxRemoveFile(first);
	wxRemoveFile(second);

	auto once = [&](const wxString& out) {
		ASSERT_EQ(setenv("OES_LAUNCH_CHILD", "1", 1), 0);
		ASSERT_EQ(setenv("OES_LAUNCH_OUT", out.utf8_str(), 1), 0);
		const long pid = ibExecuteWithSecrets(cmd, secrets);
		ASSERT_GT(pid, 0);
		int status = 0;
		const pid_t done = waitpid(static_cast<pid_t>(pid), &status, 0);
		EXPECT_EQ(done, static_cast<pid_t>(pid));
		EXPECT_TRUE(WIFEXITED(status));
		unsetenv("OES_LAUNCH_CHILD");
		unsetenv("OES_LAUNCH_OUT");
	};

	once(first);
	once(second);
	EXPECT_EQ(ReadFile(first), password);
	EXPECT_EQ(ReadFile(second), password);
	wxRemoveFile(first);
	wxRemoveFile(second);

	const long sleeper = ibExecuteWithSecrets(wxT("sleep 30"), secrets);
	ASSERT_GT(sleeper, 0);
	const wxString cmdlinePath = wxString::Format(wxT("/proc/%ld/cmdline"), sleeper);
	const wxString cmdline = ReadFile(cmdlinePath);
	EXPECT_FALSE(cmdline.Contains(password));
	EXPECT_FALSE(cmdline.Contains(wxT("db-secret-oes-9f3a")));
	EXPECT_FALSE(cmdline.Contains(wxT("--password=")));
	kill(static_cast<pid_t>(sleeper), SIGTERM);
	int status = 0;
	waitpid(static_cast<pid_t>(sleeper), &status, 0);
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
