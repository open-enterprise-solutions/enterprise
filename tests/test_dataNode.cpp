////////////////////////////////////////////////////////////////////////////
//	Description : ibDataNode / ibBinaryProvider round-trip + named-field tests.
//	              Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <memory>   // std::make_shared - a Child value's node
#include <cstring>

#include "backend/serialize/dataBuilder.h"

namespace {

wxMemoryBuffer Blob(const char* s) {
	wxMemoryBuffer b;
	b.AppendData(s, (size_t)strlen(s));
	return b;
}

// Root with a data blob + two same-clsid sibling children, one of which has a
// grandchild. Blobs stand in for the per-node SaveMeta payload (stage 1).
ibDataNode MakeTree() {
	ibDataNode root(1001, 1);
	root.AddField(wxT("name"), ibDataValue::String(wxT("RootName")));
	root.AddField(wxT("id"), ibDataValue::Int(99));
	root.AddField(wxT("flag"), ibDataValue::Bool(true));
	root.SetRawData(Blob("ROOT-DATA"));
	// Add both children first (each AddChild may grow root's vector), then reach
	// the first child by index to attach a grandchild — never hold a child ref
	// across a sibling AddChild.
	root.AddChild(2002, 2).SetRawData(Blob("child-A"));
	root.AddChild(2002, 3).SetRawData(Blob("child-B"));
	root.Children()[0].AddChild(3003, 4).SetRawData(Blob("grandchild"));
	return root;
}

wxMemoryBuffer WriteFull(const ibDataNode& root) {
	// Frame the root with its clsid/metaId identity (what the config container adds),
	// then the provider writes the INNER content — symmetric with ReadFull, which peels
	// the same two frames before handing the inner to Read. (Provider.Write is content
	// only: "Root CONTENT only — symmetric with Read".)
	ibWriterMemory inner;
	ibBinaryProvider().Write(root, inner);
	ibWriterMemory meta;
	meta.w_chunk((u64)root.GetMetaId(), inner.pointer(), inner.size());
	ibWriterMemory w;
	w.w_chunk((u64)root.GetClsid(), meta.pointer(), meta.size());
	return w.buffer();
}

// Peel the root's clsid + metaId framing (as the config container does), then
// hand the INNER content to provider.Read.
ibDataNode ReadFull(const wxMemoryBuffer& buf, ibClassID& clsidOut, ibMetaID& metaOut) {
	ibReaderMemory reader(buf);
	u64 clsid = 0;
	ibReaderMemory* clsidChunk = reader.open_chunk_iterator(clsid);
	EXPECT_TRUE(clsidChunk != nullptr);
	u64 metaId = 0;
	ibReaderMemory* metaChunk = clsidChunk->open_chunk_iterator(metaId);
	EXPECT_TRUE(metaChunk != nullptr);
	clsidOut = (ibClassID)clsid;
	metaOut = (ibMetaID)metaId;
	ibDataNode node((ibClassID)clsid, (ibMetaID)metaId);
	ibBinaryProvider().Read(*metaChunk, node);
	metaChunk->close();
	clsidChunk->close();
	return node;
}

} // namespace

// The linchpin: write -> read -> write must be byte-for-byte identical.
TEST(DataNode, BinaryProvider_RoundTrip_ByteIdentical) {
	ibDataNode tree = MakeTree();
	wxMemoryBuffer buf1 = WriteFull(tree);

	ibClassID clsid = 0;
	ibMetaID metaId = 0;
	ibDataNode tree2 = ReadFull(buf1, clsid, metaId);
	EXPECT_EQ(clsid, (ibClassID)1001);
	EXPECT_EQ(metaId, (ibMetaID)1);

	wxMemoryBuffer buf2 = WriteFull(tree2);

	ASSERT_EQ(buf1.GetDataLen(), buf2.GetDataLen());
	EXPECT_EQ(0, memcmp(buf1.GetData(), buf2.GetData(), buf1.GetDataLen()));
}

TEST(DataNode, BinaryProvider_RoundTrip_StructurePreserved) {
	ibDataNode tree = MakeTree();
	wxMemoryBuffer buf1 = WriteFull(tree);
	ibClassID clsid = 0;
	ibMetaID metaId = 0;
	ibDataNode t2 = ReadFull(buf1, clsid, metaId);

	ASSERT_EQ(t2.Children().size(), (size_t)2);
	EXPECT_EQ(t2.Children()[0].GetClsid(), (ibClassID)2002);
	EXPECT_EQ(t2.Children()[0].GetMetaId(), (ibMetaID)2);
	ASSERT_EQ(t2.Children()[0].Children().size(), (size_t)1);
	EXPECT_EQ(t2.Children()[0].Children()[0].GetClsid(), (ibClassID)3003);

	const wxMemoryBuffer& rd = t2.Children()[1].RawData();
	ASSERT_EQ(rd.GetDataLen(), strlen("child-B"));
	EXPECT_EQ(0, memcmp(rd.GetData(), "child-B", rd.GetDataLen()));

	// named fields survive the binary provider round-trip
	const ibDataValue* fName = t2.FindField(wxT("name"));
	ASSERT_TRUE(fName != nullptr);
	EXPECT_EQ(fName->AsString(), wxT("RootName"));
	const ibDataValue* fId = t2.FindField(wxT("id"));
	ASSERT_TRUE(fId != nullptr);
	EXPECT_EQ(fId->AsInt(), (s64)99);
	const ibDataValue* fFlag = t2.FindField(wxT("flag"));
	ASSERT_TRUE(fFlag != nullptr);
	EXPECT_TRUE(fFlag->AsBool());
}

// Typed values: SetValue<T> writes, GetValue<T> reads back (round-trip).
TEST(DataNode, NamedFields_TypedValues_RoundTrip) {
	ibDataNode n;
	n.SetValue(wxT("name"), wxString(wxT("Catalog1")));
	n.SetValue(wxT("flag"), true);
	n.SetValue(wxT("num"), (s32)-42);

	EXPECT_EQ(n.GetValue<wxString>(wxT("name")), wxT("Catalog1"));
	EXPECT_TRUE(n.GetValue<bool>(wxT("flag")));
	EXPECT_EQ(n.GetValue<s32>(wxT("num")), -42);
}

// ⭐ A DATE STORED BEFORE 2026-09 AS THE OLD EMPTY DATE READS AS THE EMPTY DATE. A date was an instant
// of the writing machine's clock and its empty date the literal -62135604000000; it is now a
// wall-clock reading (fdate.h) and the empty date the reading of 0001-01-01 00:00:00. The one door
// stored bytes come back through (ibBinaryProvider::ReadEntry) reads the old literal as the new empty
// date - so a configuration saved earlier keeps every date nobody filled in empty. Any other stored
// number is read as the reading it names.
TEST(DataNode, TheOldEmptyDateReadsAsTheEmptyDate) {
	ibDataNode written(1001, 7);
	written.AddField(wxT("was_empty"), ibDataValue::Date(-62135604000000ll));
	written.AddField(wxT("is_empty"),  ibDataValue::Date(static_cast<s64>(emptyDate)));
	written.AddField(wxT("a_date"),    ibDataValue::Date(static_cast<s64>(ibWallFromParts(2026, 3, 29, 2, 30))));
	written.AddField(wxT("no_date"),   ibDataValue::Date(0));

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(WriteFull(written), clsid, metaId);
	ASSERT_TRUE(read.FindField(wxT("was_empty")) != nullptr);
	EXPECT_EQ(static_cast<s64>(emptyDate), read.FindField(wxT("was_empty"))->AsDate());
	EXPECT_EQ(static_cast<s64>(emptyDate), read.FindField(wxT("is_empty"))->AsDate());
	EXPECT_EQ(static_cast<s64>(ibWallFromParts(2026, 3, 29, 2, 30)), read.FindField(wxT("a_date"))->AsDate());
	EXPECT_EQ(0, read.FindField(wxT("no_date"))->AsDate());

	// ...and through the typed codec, the parts cross the bridge to wxDateTime and back unchanged.
	const wxDateTime crossed = read.GetValue<wxDateTime>(wxT("a_date"));
	EXPECT_EQ(2026, crossed.GetYear()); EXPECT_EQ(wxDateTime::Mar, crossed.GetMonth()); EXPECT_EQ(29, crossed.GetDay());
	EXPECT_FALSE(read.GetValue<wxDateTime>(wxT("no_date")).IsValid());
	ibDataNode again;
	again.SetValue(wxT("d"), wxDateTime(15, wxDateTime::Jan, 2025, 10, 30, 0));
	EXPECT_EQ(static_cast<s64>(ibWallFromParts(2025, 1, 15, 10, 30)), again.FindField(wxT("d"))->AsDate());
}

// Optimistic cursor: out-of-order access still resolves; absent name defaults.
TEST(DataNode, NamedFields_OptimisticCursor_ReorderedAndMissing) {
	ibDataNode n;
	n.SetValue(wxT("a"), wxString(wxT("A")));
	n.SetValue(wxT("b"), wxString(wxT("B")));
	n.SetValue(wxT("c"), wxString(wxT("C")));

	EXPECT_EQ(n.GetValue<wxString>(wxT("c")), wxT("C"));
	EXPECT_EQ(n.GetValue<wxString>(wxT("a")), wxT("A"));
	EXPECT_EQ(n.GetValue<wxString>(wxT("b")), wxT("B"));
	// absent name -> default-constructed (empty)
	EXPECT_TRUE(n.GetValue<wxString>(wxT("zzz")).IsEmpty());
}

// A node written under format 1 (before 2026-09) holds INSTANTS - milliseconds of real time - where a
// format-2 node holds wall-clock readings. Read back, a format-1 date is the instant's parts on this
// machine's clock (the bridge), the old empty literal is the empty date, and 0 (no date) stays 0. The
// format-1 stream is the format-2 one with its stamp lowered: the entries' bytes are the same, only
// what they MEAN differs, which is exactly what the stamp is for.
TEST(DataNode, AFormatOneNodeReadsItsDatesAsInstants) {
	const wxDateTime instant(15, wxDateTime::Jan, 2025, 10, 30, 0);   // an instant, as an old build kept a date
	const s64 ms = instant.GetValue().GetValue();

	ibDataNode written(1001, 7);
	written.AddField(wxT("stamp"),     ibDataValue::Date(ms));
	written.AddField(wxT("was_empty"), ibDataValue::Date(-62135604000000ll));
	written.AddField(wxT("no_date"),   ibDataValue::Date(0));
	// ...and a date in every other place a node keeps one: a property, an array, a Child value's field.
	written.SetProperty(wxT("prop"), ibDataValue::Date(ms));
	written.AddField(wxT("list"), ibDataValue::Array({ ibDataValue::Date(ms), ibDataValue::Date(0) }));
	auto inner = std::make_shared<ibDataNode>();
	inner->AddField(wxT("when"), ibDataValue::Date(ms));
	written.AddField(wxT("child"), ibDataValue::Child(inner));
	wxMemoryBuffer buf = WriteFull(written);

	// The meta block: chunk id 0x2350 (u64), size (u64), then the format stamp (u32) - lowered to 1.
	const unsigned char id[8] = { 0x50, 0x23, 0, 0, 0, 0, 0, 0 };
	unsigned char* bytes = static_cast<unsigned char*>(buf.GetData());
	size_t at = buf.GetDataLen();
	int found = 0;
	for (size_t i = 0; i + 8 + 8 + 4 <= buf.GetDataLen(); ++i)
		if (memcmp(bytes + i, id, 8) == 0) { at = i + 16; ++found; }
	ASSERT_EQ(1, found) << "one meta block in the stream";
	ASSERT_EQ(2u, bytes[at]) << "written under format 2";
	bytes[at] = 1;

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(buf, clsid, metaId);
	EXPECT_EQ(static_cast<s64>(ibWallOfDateTime(instant)), read.FindField(wxT("stamp"))->AsDate());
	EXPECT_EQ(static_cast<s64>(ibWallFromParts(2025, 1, 15, 10, 30)), read.FindField(wxT("stamp"))->AsDate());
	EXPECT_EQ(static_cast<s64>(emptyDate), read.FindField(wxT("was_empty"))->AsDate());
	EXPECT_EQ(0, read.FindField(wxT("no_date"))->AsDate());
	const s64 crossed = static_cast<s64>(ibWallOfDateTime(instant));
	ASSERT_TRUE(read.FindProperty(wxT("prop")) != nullptr);
	EXPECT_EQ(crossed, read.FindProperty(wxT("prop"))->AsDate());
	ASSERT_TRUE(read.FindField(wxT("list")) != nullptr);
	ASSERT_EQ(2u, read.FindField(wxT("list"))->AsArray().size());
	EXPECT_EQ(crossed, read.FindField(wxT("list"))->AsArray()[0].AsDate());
	EXPECT_EQ(0,       read.FindField(wxT("list"))->AsArray()[1].AsDate());
	ASSERT_TRUE(read.FindField(wxT("child")) != nullptr);
	EXPECT_EQ(crossed, read.FindField(wxT("child"))->AsChild()->FindField(wxT("when"))->AsDate());
}
