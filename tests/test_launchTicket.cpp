#include <gtest/gtest.h>

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>

#include "core/launchTicket.h"

TEST(LaunchTicket, APasswordRoundTripsAndTheFileIsDeleted) {
	ibLaunchSecrets in;
	in.m_dbUser = wxT("db user");
	in.m_dbPassword = wxString::FromUTF8("p@ss \"word\" \xD0\x9F");
	in.m_ibUser = wxT("ib");
	in.m_ibPassword = wxT("ib secret");

	const wxString path = ibWriteLaunchTicket(in);
	ASSERT_FALSE(path.IsEmpty());

	wxStructStat st;
	ASSERT_EQ(wxStat(path, &st), 0);
	EXPECT_EQ(st.st_mode & 0777, 0600);

	ibLaunchSecrets out;
	ASSERT_TRUE(ibReadLaunchTicket(path, out));
	EXPECT_EQ(out.m_dbUser, in.m_dbUser);
	EXPECT_EQ(out.m_dbPassword, in.m_dbPassword);
	EXPECT_EQ(out.m_ibUser, in.m_ibUser);
	EXPECT_EQ(out.m_ibPassword, in.m_ibPassword);
	EXPECT_FALSE(wxFileExists(path));
	EXPECT_FALSE(ibReadLaunchTicket(path, out));
}

TEST(LaunchTicket, TheCommandLineDoesNotCarryThePassword) {
	wxString cmd = wxT("enterprise");
	ibLaunchSecrets in;
	in.m_dbPassword = wxT("db-secret-oes-9f3a");
	in.m_ibPassword = wxT("ib-secret-oes-9f3a");
	const wxString path = ibAppendLaunchTicket(cmd, in);
	ASSERT_FALSE(path.IsEmpty());
	EXPECT_TRUE(cmd.Contains(wxT("--cred-file=\"")));
	EXPECT_FALSE(cmd.Contains(wxT("db-secret-oes-9f3a")));
	EXPECT_FALSE(cmd.Contains(wxT("ib-secret-oes-9f3a")));
	EXPECT_FALSE(cmd.Contains(wxT("--password=")));
	EXPECT_FALSE(cmd.Contains(wxT("--ibpwd=")));

	ibLaunchSecrets out;
	ASSERT_TRUE(ibReadLaunchTicket(path, out));
	EXPECT_EQ(out.m_dbPassword, in.m_dbPassword);
	EXPECT_EQ(out.m_ibPassword, in.m_ibPassword);
}

TEST(LaunchTicket, NoPasswordWritesNoFile) {
	wxString cmd = wxT("enterprise");
	ibLaunchSecrets in;
	in.m_dbUser = wxT("only-a-name");
	EXPECT_TRUE(ibAppendLaunchTicket(cmd, in).IsEmpty());
	EXPECT_EQ(cmd, wxT("enterprise"));
}

TEST(LaunchTicket, AFieldPastTheLimitIsRefused) {
	ibLaunchSecrets in;
	in.m_dbPassword.assign(ibLaunchSecrets::kMaxFieldBytes + 1, wxT('a'));
	EXPECT_TRUE(ibWriteLaunchTicket(in).IsEmpty());
}

TEST(LaunchTicket, TheFileReplacesAPasswordThatArrivedOnTheCommandLine) {
	ibLaunchSecrets in;
	in.m_dbPassword = wxT("from-file-db");
	in.m_ibPassword = wxT("from-file-ib");
	in.m_ibUser = wxT("from-file-user");
	const wxString path = ibWriteLaunchTicket(in);
	ASSERT_FALSE(path.IsEmpty());

	wxString dbUser = wxT("kept");
	wxString dbPassword = wxT("from-argv");
	wxString ibUser;
	wxString ibPassword = wxT("from-argv-ib");
	ibTakeLaunchCredentials(path, true, true, dbUser, dbPassword, ibUser, ibPassword);
	EXPECT_EQ(dbUser, wxT("kept"));
	EXPECT_EQ(dbPassword, wxT("from-file-db"));
	EXPECT_EQ(ibUser, wxT("from-file-user"));
	EXPECT_EQ(ibPassword, wxT("from-file-ib"));
	EXPECT_FALSE(wxFileExists(path));
}

TEST(LaunchTicket, ADamagedFileIsDeletedAndRefused) {
	const wxString path = wxFileName::CreateTempFileName(wxT("oes-cred-bad-"));
	ASSERT_FALSE(path.IsEmpty());
	{
		wxFile file;
		ASSERT_TRUE(file.Open(path, wxFile::write));
		ASSERT_EQ(file.Write("nope", 4), static_cast<size_t>(4));
	}
	ibLaunchSecrets out;
	EXPECT_FALSE(ibReadLaunchTicket(path, out));
	EXPECT_FALSE(wxFileExists(path));
}
