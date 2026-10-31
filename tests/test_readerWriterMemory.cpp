// =============================================================================
// OES Enterprise — ibWriterMemory / ibReaderMemory tests
//
// The binary wire codec (core/memory/memoryStream.h) underlies metadata blobs,
// ibNumber buffers, the column wire codec and client/server exchange. A
// primitive written must read back byte-identical, and the chunk framing must
// round-trip. Pure (in-memory, no files / DB).
//
// NOTE: ibReaderMemory made on an ibMemoryBuffer holds it — one more owner of its
// bytes — so the buffer may as well be a temporary.
// =============================================================================

#include <gtest/gtest.h>
#include "core/memory/memoryStream.h"   // ibWriterMemory / ibReaderMemory
#include "core/exception.h"       // a read past the block is a refusal, in every build

TEST(ReaderWriterMemory, PrimitiveRoundTrip) {
    ibWriterMemory w;
    w.w_u32(0xDEADBEEFu);
    w.w_s32(-12345);
    w.w_u8(7);
    w.w_float(1.5f);

    const ibMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    EXPECT_EQ(r.r_u32(), 0xDEADBEEFu);
    EXPECT_EQ(r.r_s32(), -12345);
    EXPECT_EQ((int)r.r_u8(), 7);
    EXPECT_EQ(r.r_float(), 1.5f);            // exactly representable
}

TEST(ReaderWriterMemory, StringZRoundTrip) {
    ibWriterMemory w;
    w.w_stringZ(ibString(wxT("hello world")));

    const ibMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    ibString s;
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

    const ibMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    EXPECT_FALSE(r.eof());
    EXPECT_EQ(r.elapsed(), 4);
    (void)r.r_u32();
    EXPECT_TRUE(r.eof());
}

TEST(ReaderWriterMemory, ChunkReadBack) {
    const std::uint32_t payload = 0xABCD1234u;
    ibWriterMemory w;
    w.w_chunk(7ull, (void*)&payload, sizeof(payload));

    const ibMemoryBuffer buf = w.buffer();
    ibReaderMemory r(buf);
    std::uint32_t out = 0;
    EXPECT_TRUE(r.r_chunk_safe(7ull, &out, sizeof(out)));
    EXPECT_EQ(out, payload);
}

// A declared read longer than the block, and a C string with no NUL inside it,
// used to walk off the buffer. Release builds compiled the old guards out
// (wxASSERT), and r_stringZ measured the string with strlen from the cursor.
TEST(ReaderWriterMemory, ReadPastEnd_Refused) {
    unsigned char bytes[1] = { 0x01 };
    ibReaderMemory reader(bytes, 1);
    EXPECT_THROW(reader.r_u16(), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);
}

TEST(ReaderWriterMemory, AdvancePastEnd_Refused) {
    unsigned char bytes[4] = { 1, 2, 3, 4 };
    ibReaderMemory reader(bytes, 4);
    EXPECT_THROW(reader.advance(5), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);
    EXPECT_THROW(reader.seek(5), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);
    reader.advance(4);
    EXPECT_TRUE(reader.eof());
    EXPECT_THROW(reader.advance(1), ibCoreException);
}

TEST(ReaderWriterMemory, StringZWithoutTerminator_Refused) {
    char bytes[4] = { 'a', 'b', 'c', 'd' };
    ibReaderMemory reader(bytes, 4);
    EXPECT_THROW(reader.r_stringZ(), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);

    std::string out;
    EXPECT_THROW(reader.r_stringZ(out), ibCoreException);
    ibString wide;
    EXPECT_THROW(reader.r_stringZ(wide), ibCoreException);
    EXPECT_THROW(reader.skip_stringZ(), ibCoreException);
    char dest[8] = {};
    EXPECT_THROW(reader.r_stringZ(dest, sizeof(dest)), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);
}

TEST(ReaderWriterMemory, StringZStopsAtTerminator) {
    char bytes[4] = { 'h', 'i', '\0', 'x' };
    ibReaderMemory reader(bytes, 4);
    EXPECT_EQ(reader.r_stringZ(), wxT("hi"));
    EXPECT_EQ(reader.tell(), 3);
    EXPECT_EQ(reader.r_u8(), (std::uint8_t)'x');
}

TEST(ReaderWriterMemory, StringZLongerThanDestination_Refused) {
    char bytes[3] = { 'h', 'i', '\0' };
    ibReaderMemory reader(bytes, 3);
    char dest[2] = {};
    EXPECT_THROW(reader.r_stringZ(dest, sizeof(dest)), ibCoreException);
    EXPECT_EQ(reader.tell(), 0);
    char room[3] = {};
    reader.r_stringZ(room, sizeof(room));
    EXPECT_STREQ(room, "hi");
    EXPECT_TRUE(reader.eof());
}
