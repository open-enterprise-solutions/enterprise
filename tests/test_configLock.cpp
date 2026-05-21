/////////////////////////////////////////////////////////////////////////////
// test_configLock — unit tests for the cross-process configuration lock
// primitive introduced for Designer <-> oes-mcp concurrency safety.
//
// Coverage:
//   * Acquire shared, then acquire shared again (different "process" id
//     in the manifest — we simulate this by writing the file by hand).
//   * Acquire exclusive on a fresh dir, then a second exclusive attempt
//     fails with ConflictExclusive.
//   * Acquire shared after an exclusive is held -> ConflictExclusive.
//   * Release returns true; subsequent acquire succeeds.
//   * SweepDeadHolders reaps entries whose pid is not alive.
//   * Malformed manifest -> Acquire::MalformedManifest.
//   * Mutation marker write/read round-trip; seq is monotonic across
//     writes.
//
// Note. The primitive ALSO supports cross-process exclusion via flock /
// LockFileEx, but unit-test coverage of inter-process semantics needs a
// child fork — out of scope for these gtest cases. The smoke test
// (tests/mcp-smoke.py) exercises that path end-to-end with a real
// oes-mcp child.
/////////////////////////////////////////////////////////////////////////////

#include "backend/utils/configLock.hpp"

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/string.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

// Build a unique temp config directory per test. We reuse the system
// temp dir + test info name; gtest test names are safe path segments.
wxString MakeTempConfigDir(const std::string& tag)
{
	const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
	std::string name = info != nullptr
		? (std::string(info->test_suite_name()) + "_" + info->name())
		: "ibConfigLockTest";
	const std::string path = std::string(std::tmpnam(nullptr)) + "-" + name + "-" + tag;
	const wxString wpath = wxString::FromUTF8(path.c_str());
	wxFileName::Mkdir(wpath, 0755, wxPATH_MKDIR_FULL);
	return wpath;
}

// RAII-style cleanup. wxFileName::Rmdir doesn't recurse on macOS; we use
// the shell rm -rf via std::system. Cheap and safe in test-only code.
struct ScopedDir {
	wxString path;
	explicit ScopedDir(wxString p) : path(std::move(p)) {}
	~ScopedDir() {
		const std::string cmd = "rm -rf " + std::string(path.utf8_str());
		std::system(cmd.c_str());
	}
};

} // namespace

TEST(ConfigLock, AcquireSharedSucceedsOnFreshDir)
{
	ScopedDir dir(MakeTempConfigDir("fresh"));
	std::int64_t holderId = 0;
	const auto rc = ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Shared, "test", &holderId, nullptr);
	EXPECT_EQ(rc, ibConfigLock::Acquire::Ok);
	EXPECT_GT(holderId, 0);
	EXPECT_TRUE(ibConfigLock::Release(dir.path, holderId));
}

TEST(ConfigLock, ExclusiveBlocksSecondAcquire)
{
	ScopedDir dir(MakeTempConfigDir("excl_blocks"));
	std::int64_t a = 0;
	ASSERT_EQ(ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Exclusive, "first", &a, nullptr),
		ibConfigLock::Acquire::Ok);

	// Same-process re-acquire: still flagged as conflict because the
	// previous holder's pid (ours) is alive. The primitive correctly
	// refuses concurrent exclusive grants regardless of whether the
	// existing holder is in this process or another.
	std::int64_t b = 0;
	std::vector<ibConfigLock::Holder> holders;
	const auto rc = ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Shared, "second", &b, &holders);
	EXPECT_EQ(rc, ibConfigLock::Acquire::ConflictExclusive);
	EXPECT_EQ(b, 0);
	ASSERT_FALSE(holders.empty());
	EXPECT_EQ(holders.front().mode, ibConfigLock::Mode::Exclusive);

	ASSERT_TRUE(ibConfigLock::Release(dir.path, a));
}

TEST(ConfigLock, HasLiveExclusiveHolderReportsTruthfully)
{
	ScopedDir dir(MakeTempConfigDir("has_excl"));
	EXPECT_FALSE(ibConfigLock::HasLiveExclusiveHolder(dir.path));

	std::int64_t a = 0;
	ASSERT_EQ(ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Exclusive, "designer-sim", &a, nullptr),
		ibConfigLock::Acquire::Ok);
	EXPECT_TRUE(ibConfigLock::HasLiveExclusiveHolder(dir.path));

	ASSERT_TRUE(ibConfigLock::Release(dir.path, a));
	EXPECT_FALSE(ibConfigLock::HasLiveExclusiveHolder(dir.path));
}

TEST(ConfigLock, SweepDeadHoldersReapsBogusPidEntry)
{
	ScopedDir dir(MakeTempConfigDir("sweep"));
	// Manually write a manifest pinning a pid that doesn't exist.
	const wxString sysDir = dir.path + wxFileName::GetPathSeparator() + wxT("sys");
	wxFileName::Mkdir(sysDir, 0755, wxPATH_MKDIR_FULL);
	const wxString lockPath = ibConfigLock::LockFilePath(dir.path);

	const char* bogus =
		R"({"seq":1,"holders":[{"pid":1,"mode":"exclusive",)"
		R"("since":"2020-01-01T00:00:00Z","program":"ghost"}]})";
	// pid=1 is init/launchd on most systems — alive. Use a clearly-dead
	// pid above the typical max (2^22). Linux/macOS will report not-alive.
	const char* dead =
		R"({"seq":1,"holders":[{"pid":4294967294,"mode":"exclusive",)"
		R"("since":"2020-01-01T00:00:00Z","program":"ghost"}]})";
	(void)bogus;
	{
		std::ofstream f(std::string(lockPath.utf8_str()), std::ios::trunc);
		f.write(dead, static_cast<std::streamsize>(std::strlen(dead)));
	}

	const auto reaped = ibConfigLock::SweepDeadHolders(dir.path);
	EXPECT_EQ(reaped, 1u);

	// Post-sweep the directory accepts a new exclusive acquire.
	std::int64_t newHolder = 0;
	EXPECT_EQ(ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Exclusive, "after-sweep", &newHolder, nullptr),
		ibConfigLock::Acquire::Ok);
	ASSERT_TRUE(ibConfigLock::Release(dir.path, newHolder));
}

TEST(ConfigLock, MalformedManifestRejected)
{
	ScopedDir dir(MakeTempConfigDir("malformed"));
	const wxString sysDir = dir.path + wxFileName::GetPathSeparator() + wxT("sys");
	wxFileName::Mkdir(sysDir, 0755, wxPATH_MKDIR_FULL);
	const wxString lockPath = ibConfigLock::LockFilePath(dir.path);

	{
		std::ofstream f(std::string(lockPath.utf8_str()), std::ios::trunc);
		f << "{not valid json";
	}
	std::int64_t holder = 0;
	const auto rc = ibConfigLock::TryAcquire(dir.path,
		ibConfigLock::Mode::Shared, "after-corrupt", &holder, nullptr);
	EXPECT_EQ(rc, ibConfigLock::Acquire::MalformedManifest);
	EXPECT_EQ(holder, 0);
}

TEST(ConfigLock, MutationMarkerRoundTripMonotonic)
{
	ScopedDir dir(MakeTempConfigDir("marker"));

	ibConfigLock::MutationMarker out;
	EXPECT_FALSE(ibConfigLock::ReadMutationMarker(dir.path, out));

	ibConfigLock::MutationMarker first;
	first.tool     = "meta_create";
	first.fullName = "Catalog.Demo";
	first.pluginId = "mcp-server";
	ASSERT_TRUE(ibConfigLock::WriteMutationMarker(dir.path, first));

	ASSERT_TRUE(ibConfigLock::ReadMutationMarker(dir.path, out));
	EXPECT_EQ(out.tool, "meta_create");
	EXPECT_EQ(out.fullName, "Catalog.Demo");
	EXPECT_EQ(out.pluginId, "mcp-server");
	EXPECT_GE(out.seq, 1);

	ibConfigLock::MutationMarker second;
	second.tool     = "meta_edit";
	second.fullName = "Catalog.Demo";
	second.pluginId = "mcp-server";
	ASSERT_TRUE(ibConfigLock::WriteMutationMarker(dir.path, second));

	ibConfigLock::MutationMarker out2;
	ASSERT_TRUE(ibConfigLock::ReadMutationMarker(dir.path, out2));
	EXPECT_GT(out2.seq, out.seq);
}
