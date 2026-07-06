// =============================================================================
// OES Enterprise — ibSourceDescription / ibSourceHop tests
//
// ibSourceDescription (backend/sourceDescription.h) is the "passport" of a value's
// address inside a data source: an ordered list of HOPS. Each hop is {id, expected
// type} — WHERE to step (a form-local attribute id at the head, a source-column id
// deeper) and the type PINNED there (a composite reference's picked branch, else
// undefined). It is the metadata-INDEPENDENT address the source walks against
// itself (GetValueBySourceHop per hop); the id -> guid / metaData coupling is gone.
//
// Pure — no DB / appData / metadata. The serializer stores ids + types VERBATIM
// (v2 raw layout), so a round-trip is a pure in-memory byte exercise.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/sourceDescription.h"          // ibSourceHop / ibSourceDescription / ibSourceDescriptionMemory
#include "backend/clsid.h"                       // reference_to_clsid (a pinned reference branch)
#include "backend/fileSystem/fs.h"               // ibReaderMemory / ibWriterMemory (serialize round-trip)
#include "backend/serialize/dataBuilder.h"       // ibDataValue (node-form round-trip)

// ---------------------------------------------------------------------------
// ibSourceHop identity — the pinned type is PART of the hop, so a retyped
// reference reads as a DIFFERENT hop (a broken binding), not a silent mis-hop.
// ---------------------------------------------------------------------------
TEST(SourceHop, EqualityByIdAndType) {
    const ibSourceHop a{ 5, reference_to_clsid(1) };
    const ibSourceHop b{ 5, reference_to_clsid(1) };
    const ibSourceHop c{ 5, reference_to_clsid(2) };   // same id, DIFFERENT pinned branch
    const ibSourceHop d{ 6, reference_to_clsid(1) };   // different id

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a != d);
}

TEST(SourceHop, DefaultsAreUndefined) {
    const ibSourceHop h{};
    EXPECT_EQ(h.m_id, wxNOT_FOUND);
    EXPECT_EQ(h.m_type, g_valueUndefinedCLSID);   // no pin imposed until the picker sets one
}

// ---------------------------------------------------------------------------
// Empty passport: not OK, zero hops, not a dot-walk; the accessors don't crash.
// ---------------------------------------------------------------------------
TEST(SourceDescription, DefaultIsNotOk) {
    ibSourceDescription desc;
    EXPECT_FALSE(desc.IsOk());
    EXPECT_EQ(desc.GetHopCount(), 0u);
    EXPECT_FALSE(desc.IsDotWalk());
    EXPECT_EQ(desc.GetFirst(), wxNOT_FOUND);
    EXPECT_EQ(desc.GetLeaf(),  wxNOT_FOUND);
}

// ---------------------------------------------------------------------------
// One hop = a plain column: OK, GetHopCount 1, NOT a dot-walk, first == leaf.
// ---------------------------------------------------------------------------
TEST(SourceDescription, SingleHopIsPlainColumn) {
    ibSourceDescription desc(7);   // ctor from a single id
    EXPECT_TRUE(desc.IsOk());
    EXPECT_EQ(desc.GetHopCount(), 1u);
    EXPECT_FALSE(desc.IsDotWalk());
    EXPECT_EQ(desc.GetFirst(), 7);
    EXPECT_EQ(desc.GetLeaf(),  7);
    EXPECT_EQ(desc.GetExpectedType(0), g_valueUndefinedCLSID);   // no pin by default
}

// ---------------------------------------------------------------------------
// More than one hop = a real dot-walk (Ref.Ref.Field). GetByIdx windows it;
// an out-of-range index is wxNOT_FOUND, not a crash.
// ---------------------------------------------------------------------------
TEST(SourceDescription, MultiHopIsDotWalk) {
    ibSourceDescription desc(std::vector<ibSourceId>{ 1, 2, 3 });
    EXPECT_EQ(desc.GetHopCount(), 3u);
    EXPECT_TRUE(desc.IsDotWalk());
    EXPECT_EQ(desc.GetFirst(), 1);
    EXPECT_EQ(desc.GetLeaf(),  3);
    EXPECT_EQ(desc.GetByIdx(1), 2);
    EXPECT_EQ(desc.GetByIdx(9), wxNOT_FOUND);
}

// ---------------------------------------------------------------------------
// AppendSource carries the correspondence: a bare id gets an undefined type,
// a pinned hop keeps the composite branch it was appended with.
// ---------------------------------------------------------------------------
TEST(SourceDescription, AppendCarriesExpectedType) {
    ibSourceDescription desc;
    desc.AppendSource(10);                              // undefined type
    desc.AppendSource(20, reference_to_clsid(1001));   // pinned composite branch

    EXPECT_EQ(desc.GetHopCount(), 2u);
    EXPECT_EQ(desc.GetExpectedType(0), g_valueUndefinedCLSID);
    EXPECT_EQ(desc.GetExpectedType(1), reference_to_clsid(1001));
    EXPECT_EQ(desc.GetExpectedType(5), g_valueUndefinedCLSID);   // out of range -> undefined
}

// ---------------------------------------------------------------------------
// SetDefaultSource clears then appends one; ClearSource empties.
// ---------------------------------------------------------------------------
TEST(SourceDescription, SetDefaultReplaces) {
    ibSourceDescription desc(std::vector<ibSourceId>{ 1, 2, 3 });
    desc.SetDefaultSource(99);
    EXPECT_EQ(desc.GetHopCount(), 1u);
    EXPECT_EQ(desc.GetFirst(), 99);
}

TEST(SourceDescription, ClearEmptiesPath) {
    ibSourceDescription desc(std::vector<ibSourceId>{ 1, 2 });
    desc.ClearSource();
    EXPECT_FALSE(desc.IsOk());
    EXPECT_EQ(desc.GetHopCount(), 0u);
}

// ---------------------------------------------------------------------------
// Serialize round-trip (v2 raw layout): ids AND pinned types survive byte-for-
// byte — no metadata touched. This is the whole point: the passport is a
// self-contained address, so it (de)serialises without a config open.
// ---------------------------------------------------------------------------
TEST(SourceDescription, SerializeRoundTripPreservesIdAndType) {
    ibSourceDescription desc;
    desc.AppendSource(10);                              // plain hop
    desc.AppendSource(20, reference_to_clsid(1001));   // pinned composite branch
    desc.AppendSource(30);

    ibWriterMemory w;
    ASSERT_TRUE(ibSourceDescriptionMemory::SaveData(w, desc));

    const wxMemoryBuffer buf = w.buffer();   // outlives the reader (holds a pointer in)
    ibReaderMemory r(buf);

    ibSourceDescription loaded;
    ASSERT_TRUE(ibSourceDescriptionMemory::LoadData(r, loaded));

    EXPECT_EQ(loaded.GetHopCount(), 3u);
    EXPECT_EQ(loaded.GetByIdx(0), 10);
    EXPECT_EQ(loaded.GetByIdx(1), 20);
    EXPECT_EQ(loaded.GetByIdx(2), 30);
    EXPECT_EQ(loaded.GetExpectedType(0), g_valueUndefinedCLSID);
    EXPECT_EQ(loaded.GetExpectedType(1), reference_to_clsid(1001));
    EXPECT_EQ(loaded.GetExpectedType(2), g_valueUndefinedCLSID);
    EXPECT_TRUE(desc.GetPath() == loaded.GetPath());   // full hop-vector equality
}

TEST(SourceDescription, EmptySerializeRoundTrips) {
    ibSourceDescription desc;   // no hops

    ibWriterMemory w;
    ASSERT_TRUE(ibSourceDescriptionMemory::SaveData(w, desc));
    const wxMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);

    ibSourceDescription loaded(std::vector<ibSourceId>{ 1, 2 });   // seeded, must be cleared by load
    ASSERT_TRUE(ibSourceDescriptionMemory::LoadData(r, loaded));
    EXPECT_EQ(loaded.GetHopCount(), 0u);
    EXPECT_FALSE(loaded.IsOk());
}

// ---------------------------------------------------------------------------
// Node form (Binary blob in an ibDataValue) — the shape the property stores —
// round-trips through the same v2 codec.
// ---------------------------------------------------------------------------
TEST(SourceDescription, NodeRoundTrip) {
    ibSourceDescription desc;
    desc.AppendSource(5, reference_to_clsid(42));
    desc.AppendSource(6);

    ibDataValue node;
    ASSERT_TRUE(ibSourceDescriptionMemory::WriteNode(node, desc));

    ibSourceDescription loaded;
    ASSERT_TRUE(ibSourceDescriptionMemory::ReadNode(node, loaded));
    EXPECT_TRUE(desc.GetPath() == loaded.GetPath());
}
