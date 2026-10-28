////////////////////////////////////////////////////////////////////////////
//	Description : ibDataNode / ibBinaryProvider round-trip + named-field tests.
//	              Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <cstring>

#include "core/serialize/dataBuilder.h"

namespace {

ibMemoryBuffer Blob(const char* s) {
	return ibMemoryBuffer(s, strlen(s));
}

// Root with a data blob + two same-clsid sibling children, one of which has a
// grandchild. Blobs stand in for the per-node SaveMeta payload (stage 1).
ibDataNode MakeTree() {
	ibDataNode root(1001, 1);
	root.AddField(wxT("name"), ibDataValue::String(wxT("RootName")));
	root.AddField(wxT("id"), ibDataValue::Int(99));
	root.AddField(wxT("flag"), ibDataValue::Bool(true));
	root.SetRawData(Blob("ROOT-DATA"));
	// Both children first, then the first one, read out, filled where it lies (Handle) — a child read out is its bytes,
	// and a write to it alone would make a node of its own.
	root.AddChild(2002, 2).SetRawData(Blob("child-A"));
	root.AddChild(2002, 3).SetRawData(Blob("child-B"));
	root.Handle(root.Children()[0]).AddChild(3003, 4).SetRawData(Blob("grandchild"));
	return root;
}

ibMemoryBuffer WriteFull(const ibDataNode& root) {
	// Frame the root with its clsid/metaId identity (what the config container adds),
	// then the provider writes the INNER content — symmetric with ReadFull, which peels
	// the same two frames before handing the inner to Read. (Provider.Write is content
	// only: "Root CONTENT only — symmetric with Read".)
	ibWriterMemory inner;
	ibBinaryProvider().Write(root, inner);
	ibWriterMemory meta;
	meta.w_chunk((std::uint64_t)root.GetMetaId(), inner.pointer(), inner.size());
	ibWriterMemory w;
	w.w_chunk((std::uint64_t)root.GetClsid(), meta.pointer(), meta.size());
	return w.buffer();
}

// Peel the root's clsid + metaId framing (as the config container does), then
// hand the INNER content to provider.Read.
ibDataNode ReadFull(const ibMemoryBuffer& buf, ibClassID& clsidOut, ibMetaID& metaOut) {
	ibReaderMemory reader(buf);
	std::uint64_t clsid = 0;
	ibReaderMemory* clsidChunk = reader.open_chunk_iterator(clsid);
	EXPECT_TRUE(clsidChunk != nullptr);
	std::uint64_t metaId = 0;
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
	ibMemoryBuffer buf1 = WriteFull(tree);

	ibClassID clsid = 0;
	ibMetaID metaId = 0;
	ibDataNode tree2 = ReadFull(buf1, clsid, metaId);
	EXPECT_EQ(clsid, (ibClassID)1001);
	EXPECT_EQ(metaId, (ibMetaID)1);

	ibMemoryBuffer buf2 = WriteFull(tree2);

	ASSERT_EQ(buf1.GetDataLen(), buf2.GetDataLen());
	EXPECT_EQ(0, memcmp(buf1.GetData(), buf2.GetData(), buf1.GetDataLen()));
}

TEST(DataNode, BinaryProvider_RoundTrip_StructurePreserved) {
	ibDataNode tree = MakeTree();
	ibMemoryBuffer buf1 = WriteFull(tree);
	ibClassID clsid = 0;
	ibMetaID metaId = 0;
	ibDataNode t2 = ReadFull(buf1, clsid, metaId);

	ASSERT_EQ(t2.Children().size(), (size_t)2);
	EXPECT_EQ(t2.Children()[0].GetClsid(), (ibClassID)2002);
	EXPECT_EQ(t2.Children()[0].GetMetaId(), (ibMetaID)2);
	ASSERT_EQ(t2.Children()[0].Children().size(), (size_t)1);
	EXPECT_EQ(t2.Children()[0].Children()[0].GetClsid(), (ibClassID)3003);

	const ibMemoryBuffer& rd = t2.Children()[1].RawData();
	ASSERT_EQ(rd.GetDataLen(), strlen("child-B"));
	EXPECT_EQ(0, memcmp(rd.GetData(), "child-B", rd.GetDataLen()));

	// named fields survive the binary provider round-trip
	auto fName = t2.FindField(ibT("name"));
	ASSERT_TRUE(fName);
	EXPECT_EQ(fName->AsString(), wxT("RootName"));
	auto fId = t2.FindField(ibT("id"));
	ASSERT_TRUE(fId);
	EXPECT_EQ(fId->AsInt(), (std::int64_t)99);
	auto fFlag = t2.FindField(ibT("flag"));
	ASSERT_TRUE(fFlag);
	EXPECT_TRUE(fFlag->AsBool());
}

// ⭐ A NEW NODE KEEPS THE IDENTITY IT IS GIVEN — a builder's root (ibDataBuilder::Root) has no bytes until its first
// write, and BuildDataNode's first is SetClsid: the clsid went over the node's counts, the root framed itself as
// chunk(0), and a new base refused its own configuration as "another format".
TEST(DataNode, ANewNodeKeepsTheIdentityItIsGiven) {
	ibDataNode root;
	root.SetClsid(1001);
	root.SetMetaId(1);
	root.AddField(wxT("name"), ibDataValue::String(wxT("RootName")));
	root.AddChild(2002, 2);
	EXPECT_EQ(root.GetClsid(), (ibClassID)1001);
	EXPECT_EQ(root.GetMetaId(), (ibMetaID)1);

	ibClassID clsid = 0;
	ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(WriteFull(root), clsid, metaId);
	EXPECT_EQ(clsid, (ibClassID)1001);
	EXPECT_EQ(metaId, (ibMetaID)1);
	ASSERT_EQ(read.Children().size(), (size_t)1);
	ASSERT_TRUE(read.Has(ibT("name")));
	EXPECT_EQ(read.FindField(ibT("name"))->AsString(), wxT("RootName"));
}

// Typed values: SetValue<T> writes, GetValue<T> reads back (round-trip).
TEST(DataNode, NamedFields_TypedValues_RoundTrip) {
	ibDataNode n;
	n.SetValue(wxT("name"), ibString(wxT("Catalog1")));
	n.SetValue(wxT("flag"), true);
	n.SetValue(wxT("num"), (std::int32_t)-42);

	EXPECT_EQ(n.GetValue<ibString>(ibT("name")), wxT("Catalog1"));
	EXPECT_TRUE(n.GetValue<bool>(ibT("flag")));
	EXPECT_EQ(n.GetValue<std::int32_t>(ibT("num")), -42);
}

// ⭐ A DATE IS STORED AS ITSELF (format 3): an ibDateTime's count, read back as the same date, the empty
// date as the empty date - by the scalar and through the typed codec alike.
TEST(DataNode, ADateIsStoredAsItself) {
	ibDataNode written(1001, 7);
	written.AddField(wxT("is_empty"), ibDataValue::Date(ibDateTime()));
	written.AddField(wxT("a_date"),   ibDataValue::Date(ibDateTime(2026, 3, 29, 2, 30)));

	ibClassID clsid = 0; ibMetaID metaId = 0;
	const ibDataNode read = ReadFull(WriteFull(written), clsid, metaId);
	ASSERT_TRUE(read.Has(ibT("is_empty")));
	EXPECT_TRUE(read.FindField(ibT("is_empty"))->AsDate().IsEmpty());
	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), read.FindField(ibT("a_date"))->AsDate());

	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), read.GetValue<ibDateTime>(ibT("a_date")));
	EXPECT_TRUE(read.GetValue<ibDateTime>(ibT("is_empty")).IsEmpty());
	ibDataNode again;
	again.SetValue(wxT("d"), ibDateTime(2025, 1, 15, 10, 30));
	again.SetValue(wxT("none"), ibDateTime());
	EXPECT_EQ(ibDateTime(2025, 1, 15, 10, 30), again.FindField(ibT("d"))->AsDate());
	EXPECT_TRUE(again.FindField(ibT("none"))->AsDate().IsEmpty());
}

// Optimistic cursor: out-of-order access still resolves; absent name defaults.
TEST(DataNode, NamedFields_OptimisticCursor_ReorderedAndMissing) {
	ibDataNode n;
	n.SetValue(wxT("a"), ibString(wxT("A")));
	n.SetValue(wxT("b"), ibString(wxT("B")));
	n.SetValue(wxT("c"), ibString(wxT("C")));

	EXPECT_EQ(n.GetValue<ibString>(ibT("c")), wxT("C"));
	EXPECT_EQ(n.GetValue<ibString>(ibT("a")), wxT("A"));
	EXPECT_EQ(n.GetValue<ibString>(ibT("b")), wxT("B"));
	// absent name -> default-constructed (empty)
	EXPECT_TRUE(n.GetValue<ibString>(ibT("zzz")).IsEmpty());
}

