/////////////////////////////////////////////////////////////////////////////
// test_snapshotManager — unit coverage for migration::snapshots::
// ibSnapshotManager. Pure file-IO so no DB / metaBridge fixtures needed.
//
// Covers:
//   * ParseCaptureModeFromEnv handles canonical + alias values.
//   * CaptureBeforeMutation writes a well-formed JSON file to the store.
//   * List returns rows newest-first with parsed metadata fields.
//   * MarkConsumed renames the file to <id>.consumed.json.
//   * PruneToCount drops the oldest beyond the retain cap.
//   * Disabled mode skips capture without touching the disk.
/////////////////////////////////////////////////////////////////////////////

#include "backend/migration/snapshotManager.hpp"
#include "3rdparty/nlohmann/json.hpp"

#include <gtest/gtest.h>

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <string>

namespace ms = migration::snapshots;

namespace {

class SnapshotManagerTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		// Per-test scratch dir under the system temp tree. wxFileName's
		// CreateTempFileName gives us a unique path; we then convert it
		// into a directory by removing the file and Mkdir'ing.
		wxString seed = wxFileName::CreateTempFileName(wxT("oes-snap-test-"));
		// CreateTempFileName actually creates an empty file — delete it
		// so we can Mkdir at the same path.
		wxRemoveFile(seed);
		m_configDir = seed;
		ASSERT_TRUE(wxFileName::Mkdir(m_configDir, 0700, wxPATH_MKDIR_FULL));
	}

	void TearDown() override
	{
		// Best-effort cleanup — non-fatal if the recursive rm fails.
		if (!m_configDir.IsEmpty()) wxFileName::Rmdir(m_configDir, wxPATH_RMDIR_RECURSIVE);
	}

	wxString m_configDir;
};

TEST(SnapshotManagerEnv, ParseCaptureModeHandlesAliases)
{
	EXPECT_EQ(ms::CaptureMode::Full,     ms::ParseCaptureModeFromEnv(nullptr));
	EXPECT_EQ(ms::CaptureMode::Full,     ms::ParseCaptureModeFromEnv(""));
	EXPECT_EQ(ms::CaptureMode::Full,     ms::ParseCaptureModeFromEnv("true"));
	EXPECT_EQ(ms::CaptureMode::Full,     ms::ParseCaptureModeFromEnv("TRUE"));
	EXPECT_EQ(ms::CaptureMode::Disabled, ms::ParseCaptureModeFromEnv("false"));
	EXPECT_EQ(ms::CaptureMode::Disabled, ms::ParseCaptureModeFromEnv("0"));
	EXPECT_EQ(ms::CaptureMode::Disabled, ms::ParseCaptureModeFromEnv("no"));
	EXPECT_EQ(ms::CaptureMode::HashOnly, ms::ParseCaptureModeFromEnv("hash-only"));
	EXPECT_EQ(ms::CaptureMode::HashOnly, ms::ParseCaptureModeFromEnv("HASH"));
}

TEST_F(SnapshotManagerTest, CaptureWritesJsonFile)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Full);
	const wxString prior = wxT("{\"fullName\":\"Catalog.Test\",\"kind\":\"Catalog\"}");
	const wxString id = mgr.CaptureBeforeMutation(
		wxT("meta_edit"), wxT("Catalog.Test"), prior);
	ASSERT_FALSE(id.IsEmpty());

	wxFileName fn;
	fn.AssignDir(mgr.StoreDir());
	fn.SetFullName(id + wxT(".json"));
	ASSERT_TRUE(wxFileExists(fn.GetFullPath()));

	// Read body and validate the schema.
	wxFile f;
	ASSERT_TRUE(f.Open(fn.GetFullPath(), wxFile::read));
	std::string buf;
	buf.resize(static_cast<std::size_t>(f.Length()));
	f.Read(&buf[0], buf.size());
	auto body = nlohmann::json::parse(buf, nullptr, false);
	ASSERT_TRUE(body.is_object());
	EXPECT_EQ(1, body.value("schemaVersion", 0));
	EXPECT_EQ("meta_edit", body.value("triggeredBy", ""));
	EXPECT_EQ("Catalog.Test", body.value("fullName", ""));
	EXPECT_EQ("edit", body.value("operation", ""));
	ASSERT_TRUE(body.contains("priorState"));
	EXPECT_EQ("Catalog", body["priorState"].value("kind", ""));
}

TEST_F(SnapshotManagerTest, CreateOperationPriorStateNull)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Full);
	const wxString id = mgr.CaptureBeforeMutation(
		wxT("meta_create"), wxT("Catalog.Brand.New"), wxT("null"));
	ASSERT_FALSE(id.IsEmpty());

	const wxString body = mgr.Load(id);
	auto parsed = nlohmann::json::parse(std::string(body.utf8_str()), nullptr, false);
	ASSERT_TRUE(parsed.is_object());
	EXPECT_EQ("create", parsed.value("operation", ""));
	EXPECT_TRUE(parsed["priorState"].is_null());
}

TEST_F(SnapshotManagerTest, DisabledModeSkipsCapture)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Disabled);
	const wxString id = mgr.CaptureBeforeMutation(
		wxT("meta_edit"), wxT("Catalog.X"), wxT("{}"));
	EXPECT_TRUE(id.IsEmpty());
	// Store dir wasn't created either — disabled mode never touches disk.
	EXPECT_FALSE(wxDirExists(mgr.StoreDir()));
}

TEST_F(SnapshotManagerTest, ListReturnsNewestFirst)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Full);
	const wxString id1 = mgr.CaptureBeforeMutation(
		wxT("meta_edit"), wxT("Catalog.A"), wxT("{}"));
	const wxString id2 = mgr.CaptureBeforeMutation(
		wxT("meta_delete"), wxT("Catalog.B"), wxT("{}"));
	const wxString id3 = mgr.CaptureBeforeMutation(
		wxT("write_module"), wxT("Catalog.C.ObjectModule"), wxT("{}"));

	const auto rows = mgr.List(50, wxInvalidDateTime);
	ASSERT_EQ(3u, rows.size());
	// Newest-first ordering — ids are monotonic within the same second.
	EXPECT_EQ(id3, rows[0].id);
	EXPECT_EQ(id2, rows[1].id);
	EXPECT_EQ(id1, rows[2].id);
	EXPECT_EQ(wxT("Catalog.C.ObjectModule"), rows[0].fullName);
	EXPECT_EQ(wxT("write_module"),           rows[0].triggeredBy);
	EXPECT_EQ(wxT("write_module"),           rows[0].operation);
}

TEST_F(SnapshotManagerTest, MarkConsumedRenamesFile)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Full);
	const wxString id = mgr.CaptureBeforeMutation(
		wxT("meta_edit"), wxT("Catalog.X"), wxT("{}"));
	ASSERT_FALSE(id.IsEmpty());
	EXPECT_TRUE(mgr.MarkConsumed(id));

	const auto rows = mgr.List(50, wxInvalidDateTime);
	ASSERT_EQ(1u, rows.size());
	EXPECT_TRUE(rows[0].consumed);
	// Load() must still resolve consumed snapshots so rollback can read
	// the body for diagnostic / replay purposes.
	EXPECT_FALSE(mgr.Load(id).IsEmpty());
}

TEST_F(SnapshotManagerTest, PruneToCountDropsOldest)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::Full);
	for (int i = 0; i < 5; ++i) {
		mgr.CaptureBeforeMutation(wxT("meta_edit"),
			wxString::Format(wxT("Catalog.X%d"), i), wxT("{}"));
	}
	EXPECT_EQ(3u, mgr.PruneToCount(2));
	const auto rows = mgr.List(50, wxInvalidDateTime);
	EXPECT_EQ(2u, rows.size());
}

TEST_F(SnapshotManagerTest, HashOnlyModeReplacesPriorStateWithDigest)
{
	ms::ibSnapshotManager mgr(m_configDir, ms::CaptureMode::HashOnly);
	const wxString prior = wxT("{\"large\":\"payload-goes-here\"}");
	const wxString id = mgr.CaptureBeforeMutation(
		wxT("meta_edit"), wxT("Catalog.X"), prior);
	ASSERT_FALSE(id.IsEmpty());

	const wxString body = mgr.Load(id);
	auto parsed = nlohmann::json::parse(std::string(body.utf8_str()), nullptr, false);
	ASSERT_TRUE(parsed.is_object());
	EXPECT_TRUE(parsed["priorState"].is_null());
	ASSERT_TRUE(parsed.contains("priorStateDigest"));
	EXPECT_GT(parsed["priorStateDigest"].value("lengthBytes", 0), 0);
	EXPECT_FALSE(parsed["priorStateDigest"].value("previewHex", "").empty());
}

} // namespace
