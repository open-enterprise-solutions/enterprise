////////////////////////////////////////////////////////////////////////////
//	Description : ibDataNode / ibBinaryProvider round-trip + named-field tests.
//	              Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <memory>   // std::make_shared - a Child value's node
#include <cstring>

#include "core/serialize/dataBuilder.h"

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

// ⭐ A DATE IS STORED AS ITSELF (format 3): an ibDateTime's count, read back as the same date, the empty
// date as the empty date - by the scalar and through the typed codec alike.
TEST(DataNode, ADateIsStoredAsItself) {
	ibDataNode written(1001, 7);
	written.AddField(wxT("is_empty"), ibDataValue::Date(ibDateTime()));
	written.AddField(wxT("a_date"),   ibDataValue::Date(ibDateTime(2026, 3, 29, 2, 30)));

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(WriteFull(written), clsid, metaId);
	ASSERT_TRUE(read.FindField(wxT("is_empty")) != nullptr);
	EXPECT_TRUE(read.FindField(wxT("is_empty"))->AsDate().IsEmpty());
	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), read.FindField(wxT("a_date"))->AsDate());

	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), read.GetValue<ibDateTime>(wxT("a_date")));
	EXPECT_TRUE(read.GetValue<ibDateTime>(wxT("is_empty")).IsEmpty());
	ibDataNode again;
	again.SetValue(wxT("d"), ibDateTime(2025, 1, 15, 10, 30));
	again.SetValue(wxT("none"), ibDateTime());
	EXPECT_EQ(ibDateTime(2025, 1, 15, 10, 30), again.FindField(wxT("d"))->AsDate());
	EXPECT_TRUE(again.FindField(wxT("none"))->AsDate().IsEmpty());
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

// The stream of a node with its format stamp lowered: the entries' bytes are the same, only what they
// MEAN differs, which is exactly what the stamp is for. The meta block is chunk id 0x2350 (u64), size
// (u64), then the stamp (u32).
static void LowerTheStamp(wxMemoryBuffer& buf, unsigned char to)
{
	const unsigned char id[8] = { 0x50, 0x23, 0, 0, 0, 0, 0, 0 };
	unsigned char* bytes = static_cast<unsigned char*>(buf.GetData());
	size_t at = buf.GetDataLen();
	int found = 0;
	for (size_t i = 0; i + 8 + 8 + 4 <= buf.GetDataLen(); ++i)
		if (memcmp(bytes + i, id, 8) == 0) { at = i + 16; ++found; }
	ASSERT_EQ(1, found) << "one meta block in the stream";
	ASSERT_EQ(3u, bytes[at]) << "written under format 3";
	bytes[at] = to;
}

// A node written under format 1 (before 2026-09) holds INSTANTS - milliseconds of real time from the
// wxDateTime epoch - where a format-3 node holds an ibDateTime's count. Read back, a format-1 date is
// the instant's parts on this machine's clock (the bridge), and the old empty literal and 0 (no date)
// are the empty date - in every place a node keeps a date.
TEST(DataNode, AFormatOneNodeReadsItsDatesAsInstants) {
	const wxDateTime instant(15, wxDateTime::Jan, 2025, 10, 30, 0);   // an instant, as an old build kept a date
	const ibDateTime raw(static_cast<long long>(instant.GetValue().GetValue()));   // its number, as the stream held it

	ibDataNode written(1001, 7);
	written.AddField(wxT("stamp"),     ibDataValue::Date(raw));
	written.AddField(wxT("was_empty"), ibDataValue::Date(ibDateTime(-62135604000000ll)));
	written.AddField(wxT("no_date"),   ibDataValue::Date(ibDateTime(0ll)));
	// ...and a date in every other place a node keeps one: a property, an array, a Child value's field.
	written.SetProperty(wxT("prop"), ibDataValue::Date(raw));
	written.AddField(wxT("list"), ibDataValue::Array({ ibDataValue::Date(raw), ibDataValue::Date(ibDateTime(0ll)) }));
	auto inner = std::make_shared<ibDataNode>();
	inner->AddField(wxT("when"), ibDataValue::Date(raw));
	written.AddField(wxT("child"), ibDataValue::Child(inner));
	wxMemoryBuffer buf = WriteFull(written);
	LowerTheStamp(buf, 1);

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(buf, clsid, metaId);
	const ibDateTime crossed(2025, 1, 15, 10, 30);   // the instant's parts on this machine's clock
	EXPECT_EQ(crossed, read.FindField(wxT("stamp"))->AsDate());
	EXPECT_EQ(ibDateTime::OfWxDateTime(instant), read.FindField(wxT("stamp"))->AsDate());
	EXPECT_TRUE(read.FindField(wxT("was_empty"))->AsDate().IsEmpty());
	EXPECT_TRUE(read.FindField(wxT("no_date"))->AsDate().IsEmpty());
	ASSERT_TRUE(read.FindProperty(wxT("prop")) != nullptr);
	EXPECT_EQ(crossed, read.FindProperty(wxT("prop"))->AsDate());
	ASSERT_TRUE(read.FindField(wxT("list")) != nullptr);
	ASSERT_EQ(2u, read.FindField(wxT("list"))->AsArray().size());
	EXPECT_EQ(crossed, read.FindField(wxT("list"))->AsArray()[0].AsDate());
	EXPECT_TRUE(read.FindField(wxT("list"))->AsArray()[1].AsDate().IsEmpty());
	ASSERT_TRUE(read.FindField(wxT("child")) != nullptr);
	EXPECT_EQ(crossed, read.FindField(wxT("child"))->AsChild()->FindField(wxT("when"))->AsDate());
}

// A node written under format 2 (the first form of the wall reading, PR #217) holds the same readings
// counted from 1970-01-01, where 0 stood for "no date". Read back each is the date it named; that
// form's empty date (0001-01-01 counted from 1970) is the empty date.
TEST(DataNode, AFormatTwoNodeCountsItsDatesFrom1970) {
	const long long from1970 = ibDateTime(2026, 3, 29, 2, 30) - ibDateTime(1970, 1, 1);

	ibDataNode written(1001, 7);
	written.AddField(wxT("a_date"),    ibDataValue::Date(ibDateTime(from1970)));
	written.AddField(wxT("was_empty"), ibDataValue::Date(ibDateTime(-62135596800000ll)));
	written.AddField(wxT("no_date"),   ibDataValue::Date(ibDateTime(0ll)));
	wxMemoryBuffer buf = WriteFull(written);
	LowerTheStamp(buf, 2);

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(buf, clsid, metaId);
	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), read.FindField(wxT("a_date"))->AsDate());
	EXPECT_TRUE(read.FindField(wxT("was_empty"))->AsDate().IsEmpty());
	EXPECT_TRUE(read.FindField(wxT("no_date"))->AsDate().IsEmpty());
}
