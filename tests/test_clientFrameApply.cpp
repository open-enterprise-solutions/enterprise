// A PATCH, MADE BY THE SERVER AND APPLIED BY THE CLIENT, GIVES THE FRAME THE SERVER DREW.
//
// The server makes the patch (ibClientFramePatch, frmserver/client/clientPatch.cpp) on its own tree and writes it as
// its port does; the thin client reads that text into its own (ibProtocolNode, frmclient/protocol) and applies it
// (framePatch.cpp) — two halves written apart, as every client of the protocol writes its own (a browser's in JS).
// What ties them is the frame: every case below draws the frame the client holds and the frame drawn now, both by
// hand — the truth is the test's own — lets the server make the patch between them, and asks the client's half to
// make the second from the first and the patch, all three as the wire carries them.

#include <gtest/gtest.h>

#include "backend/rpc/rpcMessage.h"           // ibRpcRenderNode — the port's JSON
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientPatch.h"
#include "frmclient/protocol/protocolNode.h"

namespace {

// A frame's bulk that does not change — so the patch is lighter than the frame, the only patch the server sends.
void Ballast(ibDataNode& node)
{
	node.SetValue(wxT("Ballast"), wxString(wxT('x'), 400));
}

// The node as the port writes it, read as the thin client reads it.
ibProtocolNode Wire(const ibDataNode& node)
{
	ibProtocolNode read;
	EXPECT_TRUE(ibProtocolNode::Read(std::string(ibRpcRenderNode(node).utf8_str()), read));
	return read;
}

// The server's patch from `from` to `to`, applied by the client to `from` — all three through the wire.
void ExpectApplied(const ibDataNode& from, const ibDataNode& to)
{
	ibDataNode patch;
	ASSERT_TRUE(ibClientFramePatch(from, to, patch)) << "the server would have sent the frame whole";

	ibProtocolNode held = Wire(from);
	held.Apply(Wire(patch));
	const ibProtocolNode drawn = Wire(to);
	EXPECT_TRUE(held == drawn) << "made:  " << held.Write() << "\ndrawn: " << drawn.Write();
}

ibDataNode& Control(ibDataNode& parent, ibMetaID id, const wxString& caption)
{
	ibDataNode& control = parent.AddChild(0, id);
	control.SetProp(wxT("Caption"), caption);
	control.Child(wxT("State")).SetValue(wxT("Visible"), true);
	return control;
}

} // namespace

TEST(ClientFrameApply, EntrySetAddedRemoved)
{
	ibDataNode from;
	Ballast(from);
	from.SetValue(wxT("Title"), wxString(wxT("Goods")));
	from.SetValue(wxT("Status"), wxString(wxT("ready")));
	from.SetValue(wxT("ActiveTab"), 1);

	ibDataNode to;
	Ballast(to);
	to.SetValue(wxT("Title"), wxString(wxT("Goods: list")));
	to.SetValue(wxT("ActiveTab"), 1);
	to.SetValue(wxT("Modified"), true);

	ExpectApplied(from, to);
}

TEST(ClientFrameApply, NodeEntryMergedNotReplaced)
{
	ibDataNode from;
	Ballast(from);
	ibDataNode& view = from.Child(wxT("View"));
	view.SetValue(wxT("Key"), wxString(wxT("form-1")));
	view.Child(wxT("State")).SetValue(wxT("Title"), wxString(wxT("Receipt")));
	view.FindChild(wxT("State"))->SetValue(wxT("Modified"), false);

	ibDataNode to;
	Ballast(to);
	ibDataNode& now = to.Child(wxT("View"));
	now.SetValue(wxT("Key"), wxString(wxT("form-1")));
	now.Child(wxT("State")).SetValue(wxT("Title"), wxString(wxT("Receipt")));
	now.FindChild(wxT("State"))->SetValue(wxT("Modified"), true);

	ExpectApplied(from, to);
}

TEST(ClientFrameApply, ChildrenByIdChangedRemovedInsertedReordered)
{
	ibDataNode from;
	Ballast(from);
	Control(from, 1, wxT("Number"));
	Control(from, 2, wxT("Date"));
	Control(from, 3, wxT("Counterparty"));
	Control(from, 4, wxT("Warehouse"));

	ibDataNode to;
	Ballast(to);
	Control(to, 4, wxT("Warehouse"));
	Control(to, 1, wxT("Number"));
	Control(to, 5, wxT("Comment"));
	ibDataNode& changed = Control(to, 3, wxT("Supplier"));
	changed.FindChild(wxT("State"))->SetValue(wxT("ReadOnly"), true);

	ExpectApplied(from, to);
}

TEST(ClientFrameApply, AnotherNodeUnderTheSameIdComesWhole)
{
	ibDataNode from;
	Ballast(from);
	from.AddChild(101, 1).SetProp(wxT("Caption"), wxString(wxT("A field")));
	from.AddChild(101, 2).SetProp(wxT("Caption"), wxString(wxT("Stays")));

	ibDataNode to;
	Ballast(to);
	to.AddChild(202, 1).SetProp(wxT("Title"), wxString(wxT("A table now")));
	to.AddChild(101, 2).SetProp(wxT("Caption"), wxString(wxT("Stays")));

	ExpectApplied(from, to);
}

TEST(ClientFrameApply, AnotherNodeUnderTheSameNameComesWhole)
{
	// A tab's text view, patched from a sheet's — the view kept for another tab.
	ibDataNode from;
	Ballast(from);
	ibDataNode& sheet = from.Child(wxT("View"));
	sheet.SetClsid(303);
	sheet.Child(wxT("State")).SetValue(wxT("RowCount"), 10);

	ibDataNode to;
	Ballast(to);
	to.Child(wxT("View")).SetValue(wxT("Text"), wxString(wxT("a text")));

	ExpectApplied(from, to);
}

TEST(ClientFrameApply, ChildrenWithoutIdsComeWhole)
{
	ibDataNode from;
	Ballast(from);
	ibDataNode& menu = from.Child(wxT("Menu"));
	menu.AddChild(0, 0).SetValue(wxT("Title"), wxString(wxT("File")));
	menu.AddChild(0, 0).SetValue(wxT("Title"), wxString(wxT("Edit")));

	ibDataNode to;
	Ballast(to);
	ibDataNode& now = to.Child(wxT("Menu"));
	now.AddChild(0, 0).SetValue(wxT("Title"), wxString(wxT("File")));
	now.AddChild(0, 0).SetValue(wxT("Title"), wxString(wxT("Edit")));
	now.AddChild(0, 0).SetValue(wxT("Title"), wxString(wxT("Help")));

	ExpectApplied(from, to);
}

// The node a client fills reads back as filled — and a handle read from it shares its tree.
TEST(ClientFrameApply, NodeFilledAndRead)
{
	// "Товар" in UTF-8, spelled as bytes: the build does not tell every compiler how this file is encoded.
	const char* const goods = "\xD0\xA2\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80";

	ibProtocolNode node;
	node.SetValue("Title", wxString::FromUTF8(goods))
		.SetValue("ActiveTab", 3)
		.SetValue("Locked", true);
	node.Child("View").SetValue("Key", "form-1");
	node.AddChild().SetValue("NodeId", 7).SetValue("NodeType", "Tablebox");

	ibProtocolNode read;
	ASSERT_TRUE(ibProtocolNode::Read(node.Write(), read));
	EXPECT_EQ(read.GetString("Title"), wxString::FromUTF8(goods));
	EXPECT_EQ(read.GetInt("ActiveTab"), 3);
	EXPECT_TRUE(read.GetBool("Locked"));
	EXPECT_EQ(read.FindChild("View").GetString("Key"), wxString(wxT("form-1")));
	ASSERT_EQ(read.Children().size(), 1u);
	EXPECT_EQ(read.Children()[0].GetId(), 7);
	EXPECT_EQ(read.Children()[0].GetType(), wxString(wxT("Tablebox")));
	EXPECT_TRUE(read.FindChild("Nothing").IsEmpty());
	EXPECT_EQ(read.GetInt("Title", -1), -1);   // another kind — the default
}
