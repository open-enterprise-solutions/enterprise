// A PATCH, MADE BY THE SERVER AND APPLIED BY THE CLIENT, GIVES THE FRAME THE SERVER DREW.
//
// The server makes the patch (ibClientFramePatch, frmserver/client/clientPatch.cpp) on its own tree and writes it as
// its port does — a packet; the thin client reads it where it lies (ibProtocolNode, protocol/) and applies it
// (framePatch.cpp) — two halves written apart, as every client of the protocol writes its own (a browser's in JS).
// What ties them is the frame: every case below draws the frame the client holds and the frame drawn now, both by
// hand — the truth is the test's own — lets the server make the patch between them, and asks the client's half to
// make the second from the first and the patch, all three as the wire carries them.

#include <gtest/gtest.h>

#include "core/serialize/dataBuilder.h"         // ibBinaryProvider — the port's packet

#include "frmserver/client/clientPatch.h"
#include "protocol/protocol.h"                // ibProtocol::ibForm::ibState, ibProtocol::ibEvent::ibKind — the words both halves write
#include "protocol/protocolNode.h"

namespace {

// A frame's bulk that does not change — so the patch is lighter than the frame, the only patch the server sends.
void Ballast(ibDataNode& node)
{
	node.SetValue(wxT("Ballast"), wxString(wxT('x'), 400));
}

// The node as the port writes it, read as the thin client reads it.
ibProtocolNode Wire(const ibDataNode& node)
{
	ibMemoryBuffer packet;
	ibBinaryProvider().Write(node, packet);
	ibProtocolNode read;
	EXPECT_TRUE(ibProtocolNode::Read(packet, read));
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
	EXPECT_TRUE(held == drawn) << "made:  " << held.Write().ToUtf8() << "\ndrawn: " << drawn.Write().ToUtf8();
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
	view.Child(wxT("State")).SetValue(wxT("Modified"), false);

	ibDataNode to;
	Ballast(to);
	ibDataNode& now = to.Child(wxT("View"));
	now.SetValue(wxT("Key"), wxString(wxT("form-1")));
	now.Child(wxT("State")).SetValue(wxT("Title"), wxString(wxT("Receipt")));
	now.Child(wxT("State")).SetValue(wxT("Modified"), true);

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
	changed.Child(wxT("State")).SetValue(wxT("ReadOnly"), true);

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

// A control's subscription, written by the server into its State and read by the client from the frame it holds: it
// comes with a patch, and a patch takes it away — the form modified, its fields no longer listen to Input.
TEST(ClientFrameApply, SubscriptionComesAndGoes)
{
	const ibString subscribed(ibProtocol::ibForm::ibState::Subscribed);
	const auto subscribedOf = [](const ibProtocolNode& frame) {
		std::vector<long long> events;
		const ibProtocolNode state = frame.Children()[0].FindChild(ibProtocol::ibForm::ibState::State);
		for (const ibProtocolNode& event : state.GetList(ibProtocol::ibForm::ibState::Subscribed))
			events.push_back(event.AsInt());
		return events;
	};

	ibDataNode bare;
	Ballast(bare);
	Control(bare, 1, wxT("Number"));

	ibDataNode listening;
	Ballast(listening);
	const long long input = static_cast<int>(ibProtocol::ibEvent::ibKind::Input);
	const long long row = static_cast<int>(ibProtocol::ibEvent::ibKind::Row);
	Control(listening, 1, wxT("Number")).Child(wxT("State")).AddField(subscribed,
		ibDataValue::Array(std::vector<ibDataValue>{ ibDataValue::Int(input), ibDataValue::Int(row) }));

	ExpectApplied(bare, listening);
	ExpectApplied(listening, bare);

	ibDataNode came;
	ASSERT_TRUE(ibClientFramePatch(bare, listening, came));
	ibProtocolNode held = Wire(bare);
	held.Apply(Wire(came));
	EXPECT_EQ(subscribedOf(held), (std::vector<long long>{ input, row }));

	ibDataNode went;
	ASSERT_TRUE(ibClientFramePatch(listening, bare, went));
	held.Apply(Wire(went));
	EXPECT_TRUE(subscribedOf(held).empty());
}

// The node a client fills reads back as filled — and a handle read from it shares its tree.
TEST(ClientFrameApply, NodeFilledAndRead)
{
	// "Товар" in UTF-8, spelled as bytes: the build does not tell every compiler how this file is encoded.
	const char* const goods = "\xD0\xA2\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80";

	ibProtocolNode node;
	node.SetValue("Title", ibString(goods))
		.SetValue("ActiveTab", 3)
		.SetValue("Locked", true);
	node.Child("View").SetValue("Key", "form-1");
	node.AddChild().SetValue("NodeId", 7).SetValue("NodeType", "Tablebox");

	ibProtocolNode read;
	ASSERT_TRUE(ibProtocolNode::Read(node.Write(), read));
	EXPECT_TRUE(read.GetString("Title") == goods);
	EXPECT_EQ(read.GetInt("ActiveTab"), 3);
	EXPECT_TRUE(read.GetBool("Locked"));
	EXPECT_EQ(read.FindChild("View").GetString("Key"), wxString(wxT("form-1")));
	ASSERT_EQ(read.Children().size(), 1u);
	EXPECT_EQ(read.Children()[0].GetId(), 7);
	EXPECT_EQ(read.Children()[0].GetType(), wxString(wxT("Tablebox")));
	EXPECT_TRUE(read.FindChild("Nothing").IsEmpty());
	EXPECT_EQ(read.GetInt("Title", -1), -1);   // another kind — the default
}

// …and goes as a packet, read back where it lies: the same node.
TEST(ClientFrameApply, NodeFilledGoesAsPacket)
{
	ibProtocolNode node;
	node.SetValue("Title", ibString(wxT("Goods"))).SetValue("ActiveTab", 3).SetValue("Locked", true);
	node.Child("View").SetValue("Key", "form-1");
	node.AddChild().SetValue("NodeId", 7).SetValue("NodeType", "Tablebox");
	node.AddItem("Features", ibString(wxT("resume")));

	ibMemoryBuffer packet;
	node.Write(packet);
	ibProtocolNode read;
	ASSERT_TRUE(ibProtocolNode::Read(packet, read));
	EXPECT_TRUE(read == node);
	EXPECT_EQ(read.GetInt("ActiveTab"), 3);
	EXPECT_TRUE(read.GetBool("Locked"));
	EXPECT_EQ(read.FindChild("View").GetString("Key"), wxString(wxT("form-1")));
	ASSERT_EQ(read.Children().size(), 1u);
	EXPECT_EQ(read.Children()[0].GetId(), 7);
	EXPECT_EQ(read.Children()[0].GetType(), wxString(wxT("Tablebox")));
	ASSERT_EQ(read.GetList("Features").size(), 1u);
	EXPECT_EQ(read.GetList("Features")[0].AsString(), wxString(wxT("resume")));
}

// Bytes that are no packet are refused, not read.
TEST(ClientFrameApply, BrokenPacketRefused)
{
	ibProtocolNode node;
	node.SetValue("Title", ibString(wxT("Goods")));
	ibMemoryBuffer packet;
	node.Write(packet);

	const ibMemoryBuffer cut(packet.GetData(), packet.GetDataLen() - 2);
	ibProtocolNode read;
	EXPECT_FALSE(ibProtocolNode::Read(cut, read));
}

// A handle read out of a tree made here writes into it — the designer's edit of its form's copy: a parent found by
// reading, its children given anew, read anew through the same handle.
TEST(ClientFrameApply, HandleReadFromCloneWritesIntoIt)
{
	ibProtocolNode form;
	ibProtocolNode sizer = form.AddChild();
	sizer.SetValue("ControlId", 1);
	sizer.AddChild().SetValue("ControlId", 2);
	sizer.AddChild().SetValue("ControlId", 3);

	ibProtocolNode edited = form.Clone();
	ibProtocolNode parent = edited.Children()[0];
	std::vector<ibProtocolNode> copies;
	for (const ibProtocolNode& child : parent.Children()) {
		if (child.GetInt("ControlId") != 2)
			copies.push_back(child.Clone());
	}
	parent.Remove("NodeChildren");
	for (const ibProtocolNode& child : copies) {
		ibProtocolNode added = parent.AddChild();
		for (const auto& [name, entry] : child.Entries())
			added.SetValue(name, entry);
	}

	ASSERT_EQ(parent.Children().size(), 1u);
	EXPECT_EQ(parent.Children()[0].GetInt("ControlId"), 3);
	ASSERT_EQ(edited.Children()[0].Children().size(), 1u);
	EXPECT_EQ(form.Children()[0].Children().size(), 2u);   // the one copied is as it was
}

// ⭐ A CLONE OF A PART OF A PACKET IS WALKED WHOLE — the designer's tree, a part of the frame's answer, cloned and walked
// for its pictures: the first part's handle moved the copy into a packet of its own, and every part after it was looked
// for in the bytes it had left (2026-10-26, a crash at the designer's start).
TEST(ClientFrameApply, CloneOfAPartIsWalkedWhole)
{
	ibDataNode frame;
	Ballast(frame);
	ibDataNode& tree = frame.Child(wxT("Tree"));
	tree.SetMetaId(1);
	tree.Child(wxT("First")).SetValue(wxT("Picture"), wxString(wxT("1")));
	tree.Child(wxT("Second")).SetValue(wxT("Picture"), wxString(wxT("2")));
	tree.AddChild(0, 2).SetValue(wxT("Picture"), wxString(wxT("3")));
	tree.AddChild(0, 3).SetValue(wxT("Picture"), wxString(wxT("4")));

	const ibProtocolNode cloned = Wire(frame).FindChild("Tree").Clone();
	std::vector<wxString> pictures;
	for (const auto& [name, entry] : cloned.Entries()) {
		if (entry.IsNode())
			pictures.push_back(wxString(entry.GetString("Picture")));
	}
	for (const ibProtocolNode& child : cloned.Children())
		pictures.push_back(wxString(child.GetString("Picture")));
	EXPECT_EQ(pictures, (std::vector<wxString>{ wxT("1"), wxT("2"), wxT("3"), wxT("4") }));
}
