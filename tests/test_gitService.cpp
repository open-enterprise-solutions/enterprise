////////////////////////////////////////////////////////////////////////////
//	test_gitService.cpp — end-to-end against a throwaway temp repo. Skips if
//	`git` is not on PATH (so it never red-fails a CI box without git).
////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>
#include "backend/vcs/gitService.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/file.h>
#include <wx/dir.h>
#include <wx/utils.h>   // wxExecute, wxEXEC_SYNC, wxGetProcessId

namespace {

// Unique temp working dir per test instance. We never collide because the
// directory name folds in a monotonically-bumped counter.
static int s_counter = 0;
wxString MakeTempDir() {
	wxString base = wxStandardPaths::Get().GetTempDir();
	wxString dir;
	dir << base << wxFileName::GetPathSeparator()
	    << wxT("oes_git_test_") << (++s_counter) << wxT("_") << (long)wxGetProcessId();
	wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	return dir;
}

void RmDir(const wxString& dir) {
	if (wxDirExists(dir)) wxFileName::Rmdir(dir, wxPATH_RMDIR_RECURSIVE);
}

void WriteFile(const wxString& dir, const wxString& name, const wxString& body) {
	wxFile f(dir + wxFileName::GetPathSeparator() + name, wxFile::write);
	f.Write(body);
	f.Close();
}

class GitServiceTest : public ::testing::Test {
protected:
	void SetUp() override {
		if (!ibGitService::IsGitAvailable()) GTEST_SKIP() << "git not on PATH";
		m_dir = MakeTempDir();
	}
	void TearDown() override { if (!m_dir.empty()) RmDir(m_dir); }
	wxString m_dir;
};

TEST_F(GitServiceTest, NonRepoDirIsNotRepo) {
	ibGitService g(m_dir);
	EXPECT_FALSE(g.IsRepo());
}

TEST_F(GitServiceTest, InitMakesRepo) {
	ibGitService g(m_dir);
	const ibGitResult r = g.Init();
	ASSERT_TRUE(r.ok) << r.error.ToStdString();
	EXPECT_TRUE(g.IsRepo());
}

TEST_F(GitServiceTest, UntrackedFileShowsInStatus) {
	ibGitService g(m_dir);
	ASSERT_TRUE(g.Init().ok);
	WriteFile(m_dir, wxT("Catalog.xml"), wxT("<meta/>"));
	const auto st = g.Status();
	ASSERT_EQ(st.size(), 1u);
	EXPECT_EQ(st[0].path, wxT("Catalog.xml"));
	EXPECT_TRUE(st[0].untracked);
}

TEST_F(GitServiceTest, CommitAllClearsStatusAndAppearsInLog) {
	ibGitService g(m_dir);
	ASSERT_TRUE(g.Init().ok);
	// local identity so commit doesn't fail on an unconfigured box
	wxExecute(wxT("git -C ") + m_dir + wxT(" config user.email t@oes.local"), wxEXEC_SYNC);
	wxExecute(wxT("git -C ") + m_dir + wxT(" config user.name OES"), wxEXEC_SYNC);

	WriteFile(m_dir, wxT("Document.json"), wxT("{}"));
	const ibGitResult c = g.CommitAll(wxT("add Document"));
	ASSERT_TRUE(c.ok) << c.error.ToStdString();

	EXPECT_TRUE(g.Status().empty());                 // working tree clean

	const auto log = g.Log();
	ASSERT_EQ(log.size(), 1u);
	EXPECT_EQ(log[0].subject, wxT("add Document"));
}

TEST_F(GitServiceTest, BranchCreateCheckoutRoundTrips) {
	ibGitService g(m_dir);
	ASSERT_TRUE(g.Init().ok);
	wxExecute(wxT("git -C ") + m_dir + wxT(" config user.email t@oes.local"), wxEXEC_SYNC);
	wxExecute(wxT("git -C ") + m_dir + wxT(" config user.name OES"), wxEXEC_SYNC);
	WriteFile(m_dir, wxT("a.txt"), wxT("x"));
	ASSERT_TRUE(g.CommitAll(wxT("init")).ok);

	const wxString start = g.CurrentBranch();
	ASSERT_FALSE(start.empty());

	ASSERT_TRUE(g.CreateBranch(wxT("feature/x"), /*checkout*/true).ok);
	EXPECT_EQ(g.CurrentBranch(), wxT("feature/x"));

	const wxArrayString branches = g.Branches();
	EXPECT_NE(branches.Index(wxT("feature/x")), wxNOT_FOUND);
	EXPECT_NE(branches.Index(start), wxNOT_FOUND);

	ASSERT_TRUE(g.Checkout(start).ok);
	EXPECT_EQ(g.CurrentBranch(), start);
}

} // namespace
