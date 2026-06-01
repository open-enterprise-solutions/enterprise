// =============================================================================
// OES Enterprise — metadata serialization round-trip (Step 0 linchpin)
//
// The metadata tree is serialized by hand-maintained POSITIONAL, UNTAGGED
// field lists: SaveMeta/LoadMeta on the node, SaveData/LoadData per type, and
// the node walk SaveSubtree/LoadSubtree. Reorder one line in a Save body
// without the mirror edit in the matching Load body and every object of that
// type silently corrupts on the next reload — no other test catches it.
//
// This is the regression guard the serialization arc mandates BEFORE any
// further step (see docs/metadata-serialization-arc.md, "Step 0"). It proves
// the format is a fixed point: load(save(x)) re-serializes to the identical
// bytes. Any drift between a Save body and its Load mirror flips the
// byte-equality assertion.
//
// DB-free by construction:
//   * ibMetaDataConfigurationFile has a public ctor (the standalone file-config
//     holder; the appData-owned subclasses are private + friend ibApplicationData).
//   * SaveConfigToBuffer serializes with saveToFileFlag, which makes SaveSubtree
//     skip OnSaveMetaObject (the only DB-touching hook on the save path).
//   * A freshly-built File is never "run" (m_configOpened == false), so
//     LoadConfigFromBuffer skips CloseDatabase; the create/load hooks for the
//     default tree (Configuration + Language) only set guid/id/m_metaData.
// So the round-trip needs no live ibDatabaseLayer / appData.
// =============================================================================

#include <gtest/gtest.h>

#include <cstring>

#include <wx/filename.h>   // wxFileName::CreateTempFileName
#include <wx/filefn.h>     // wxRemoveFile

#include "backend/metadataConfiguration.h"

namespace {

bool BuffersEqual(const wxMemoryBuffer& a, const wxMemoryBuffer& b) {
	if (a.GetDataLen() != b.GetDataLen())
		return false;
	if (a.GetDataLen() == 0)
		return true;
	return std::memcmp(a.GetData(), b.GetData(), a.GetDataLen()) == 0;
}

} // namespace

// The default config (Configuration root + English Language) saved by a fresh
// holder, reloaded into a second holder, then re-saved — bytes must match.
// This exercises SaveMeta/LoadMeta (guid, id, name/synonym/comment props,
// interface, roles, help) and the SaveSubtree/LoadSubtree child walk.
TEST(MetadataSerialize, DefaultConfig_RoundTrip_BytesEqual) {
	ibMetaDataConfigurationFile cfg1;

	wxMemoryBuffer buf1;
	ASSERT_TRUE(cfg1.SaveConfigToBuffer(buf1));
	ASSERT_GT(buf1.GetDataLen(), 0u) << "save produced an empty blob";

	ibMetaDataConfigurationFile cfg2;
	ASSERT_TRUE(cfg2.LoadConfigFromBuffer(buf1));

	wxMemoryBuffer buf2;
	ASSERT_TRUE(cfg2.SaveConfigToBuffer(buf2));

	ASSERT_EQ(buf1.GetDataLen(), buf2.GetDataLen())
		<< "re-serialized blob has a different length — a Save/Load mirror drifted";
	EXPECT_TRUE(BuffersEqual(buf1, buf2))
		<< "re-serialized blob differs byte-for-byte — Save/Load mirror drift";
}

// Reloading the same blob a second time must reproduce it again — the format is
// a stable fixed point, not merely equal on the first pass (guards against a
// load that consumes more/less than save wrote and only diverges on re-entry).
TEST(MetadataSerialize, DefaultConfig_RoundTrip_Idempotent) {
	ibMetaDataConfigurationFile cfg1;
	wxMemoryBuffer buf1;
	ASSERT_TRUE(cfg1.SaveConfigToBuffer(buf1));

	ibMetaDataConfigurationFile cfg2;
	ASSERT_TRUE(cfg2.LoadConfigFromBuffer(buf1));
	wxMemoryBuffer buf2;
	ASSERT_TRUE(cfg2.SaveConfigToBuffer(buf2));

	ibMetaDataConfigurationFile cfg3;
	ASSERT_TRUE(cfg3.LoadConfigFromBuffer(buf2));
	wxMemoryBuffer buf3;
	ASSERT_TRUE(cfg3.SaveConfigToBuffer(buf3));

	EXPECT_TRUE(BuffersEqual(buf2, buf3))
		<< "second reload diverged — load does not consume exactly what save wrote";
}

// A blob that round-trips through file I/O (SaveConfigToFile → LoadConfigFromFile)
// must match the in-memory buffer save — confirms the file path funnels through
// the same SaveConfigToBuffer/LoadConfigFromBuffer seam with no extra framing.
TEST(MetadataSerialize, DefaultConfig_FileRoundTrip_MatchesBuffer) {
	ibMetaDataConfigurationFile cfg1;
	wxMemoryBuffer bufMem;
	ASSERT_TRUE(cfg1.SaveConfigToBuffer(bufMem));

	const wxString path = wxFileName::CreateTempFileName(wxT("oes_meta_rt_"));
	ASSERT_FALSE(path.IsEmpty());
	ASSERT_TRUE(cfg1.SaveConfigToFile(path));

	ibMetaDataConfigurationFile cfg2;
	ASSERT_TRUE(cfg2.LoadConfigFromFile(path));
	wxMemoryBuffer bufFromFile;
	ASSERT_TRUE(cfg2.SaveConfigToBuffer(bufFromFile));

	wxRemoveFile(path);

	EXPECT_TRUE(BuffersEqual(bufMem, bufFromFile))
		<< "file round-trip diverged from the in-memory buffer seam";
}
