////////////////////////////////////////////////////////////////////////////
//	Description : ibDataNode / ibBinaryProvider round-trip + named-field tests.
//	              Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
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
	ibWriterMemory w;
	ibBinaryProvider().Write(root, w);
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
