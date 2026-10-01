// =============================================================================
// OES Enterprise — ibWriterMemory / ibReaderMemory tests
//
// The binary wire codec (backend/fileSystem/fs.h) underlies metadata blobs,
// ibNumber buffers, the column wire codec and client/server exchange. A
// primitive written must read back byte-identical, and the chunk framing must
// round-trip. Pure (in-memory, no files / DB).
//
// NOTE: ibReaderMemory holds a POINTER into the wxMemoryBuffer it is given — the
// buffer must outlive the reader, so it is kept in a named local (never passed a
// temporary).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/fileSystem/fs.h"   // ibWriterMemory / ibReaderMemory + u8/u16/u32/s32 types

TEST(ReaderWriterMemory, PrimitiveRoundTrip) {
    ibWriterMemory w;
    w.w_u32(0xDEADBEEFu);
    w.w_s32(-12345);
    w.w_u8(7);
    w.w_float(1.5f);

    const wxMemoryBuffer buf = w.buffer();   // outlives the reader
    ibReaderMemory r(buf);
    EXPECT_EQ(r.r_u32(), 0xDEADBEEFu);
    EXPECT_EQ(r.r_s32(), -12345);
    EXPECT_EQ((int)r.r_u8(), 7);
    EXPECT_EQ(r.r_float(), 1.5f);            // exactly representable
}

TEST(ReaderWriterMemory, StringZRoundTrip) {
    ibWriterMemory w;
    w.w_stringZ(wxString(wxT("hello world")));

    const wxMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    wxString s;
    r.r_stringZ(s);
    EXPECT_EQ(s, wxT("hello world"));
}

TEST(ReaderWriterMemory, SizeReflectsWrites) {
    ibWriterMemory w;
    EXPECT_EQ(w.size(), 0u);
    w.w_u32(1);
    EXPECT_EQ(w.size(), 4u);
    w.w_u16(1);
    EXPECT_EQ(w.size(), 6u);
}

TEST(ReaderWriterMemory, ReaderTracksEofAndElapsed) {
    ibWriterMemory w; w.w_u32(1);

    const wxMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    EXPECT_FALSE(r.eof());
    EXPECT_EQ(r.elapsed(), 4);
    (void)r.r_u32();
    EXPECT_TRUE(r.eof());
}

TEST(ReaderWriterMemory, ChunkReadBack) {
    const u32 payload = 0xABCD1234u;
    ibWriterMemory w;
    w.w_chunk(7ull, (void*)&payload, sizeof(payload));

    const wxMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    u32 out = 0;
    EXPECT_TRUE(r.r_chunk_safe(7ull, &out, sizeof(out)));
    EXPECT_EQ(out, payload);
}
