// =============================================================================
// OES Enterprise — a command bar's submenus: what the author makes on a form's command interface to hold commands and
// further submenus, to any depth (docs/private/command-interface.md §7a). These pin:
//
//   * a submenu holds commands and submenus, and a name is free on the whole bar — CommandN and SubmenuN counted at
//     every depth, a name taken anywhere refused;
//   * the bar goes out with its form and comes back the same, through the bytes: each child of its kind, a submenu
//     holding its own;
//   * the bar built: a submenu is a group, its entries under it, each with an id of its own — what a press names, found
//     wherever it stands; a hidden submenu goes with all it holds;
//   * a command bound to nothing is an entry that runs nothing, greyed under its name, on the bar and in a submenu.
//
// DB-FREE AND UI-FREE: a bar made by its own ctor, outside any form.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/objCtor.h"                           // ibCtorAbstractType — a bar made by its own ctor
#include "core/serialize/dataBuilder.h"                // ibDataNode, ibBinaryProvider — the bar as its form writes it

#include "frmserver/visualView/layers/commandBar.h"    // ibValueCommandBar, ibValueCommandBarSubmenu

namespace {

ibValue MakeBar()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(system_to_clsid("CommandBar"));
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

ibValueCommandBar* BarOf(const ibValue& held)
{
	return dynamic_cast<ibValueCommandBar*>(held.GetRef());
}

// Through the door the inspector, MCP and a script write through.
bool Rename(ibPropertyObject* object, const ibChar* name)
{
	return ibPropertyGate::SetValue(object, object->GetProperty(ibString(ibT("Name"))), ibVariant(ibString(name)));
}

} // namespace

TEST(CommandBarSubmenu, ASubmenuIsATypeOfItsOwn)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	EXPECT_NE(ibValue::GetAvailableCtor(g_commandBarSubmenuCLSID), nullptr);
}

TEST(CommandBarSubmenu, ANameIsFreeOnTheWholeBar)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	const ibValue held = MakeBar();
	ibValueCommandBar* const bar = BarOf(held);
	ASSERT_NE(bar, nullptr);

	ibValueCommandBarItem* const first = bar->AddCommandItem();
	ibValueCommandBarSubmenu* const submenu = bar->AddSubmenu();
	ibValueCommandBarItem* const inner = submenu->AddCommandItem();
	ibValueCommandBarSubmenu* const nested = submenu->AddSubmenu();

	EXPECT_EQ(first->GetName(), ibString(ibT("Command1")));
	EXPECT_EQ(submenu->GetName(), ibString(ibT("Submenu1")));
	EXPECT_EQ(inner->GetName(), ibString(ibT("Command2")));
	EXPECT_EQ(nested->GetName(), ibString(ibT("Submenu2")));
	EXPECT_EQ(inner->GetBar(), bar);
	EXPECT_EQ(nested->GetBar(), bar);

	EXPECT_FALSE(Rename(inner, ibT("Command1")));    // the bar's own command's
	EXPECT_FALSE(Rename(first, ibT("Submenu2")));    // a submenu's two levels down
	EXPECT_TRUE(Rename(inner, ibT("Print")));
	EXPECT_EQ(inner->GetName(), ibString(ibT("Print")));
}

TEST(CommandBarSubmenu, TheBarComesBackAsItWent)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	const ibValue held = MakeBar();
	ibValueCommandBar* const bar = BarOf(held);
	ASSERT_NE(bar, nullptr);
	bar->AddCommandItem();
	ibValueCommandBarSubmenu* const submenu = bar->AddSubmenu();
	submenu->AddCommandItem();
	submenu->AddSubmenu()->AddCommandItem();

	ibDataNode written;
	ASSERT_TRUE(bar->WriteData(written));
	ibMemoryBuffer bytes;
	ibBinaryProvider().Write(written, bytes);
	ibDataNode node;
	ibBinaryProvider().Read(bytes, node);

	const ibValue readHeld = MakeBar();
	ibValueCommandBar* const read = BarOf(readHeld);
	ASSERT_NE(read, nullptr);
	ASSERT_TRUE(read->ReadData(node));

	ASSERT_EQ(read->GetChildCount(), 2u);
	const ibValueCommandBarItem* const command = dynamic_cast<const ibValueCommandBarItem*>(read->GetChild(0));
	ASSERT_NE(command, nullptr);
	EXPECT_EQ(command->GetName(), ibString(ibT("Command1")));

	ibValueCommandBarSubmenu* const readSubmenu = dynamic_cast<ibValueCommandBarSubmenu*>(read->GetChild(1));
	ASSERT_NE(readSubmenu, nullptr);
	EXPECT_EQ(readSubmenu->GetName(), ibString(ibT("Submenu1")));
	ASSERT_EQ(readSubmenu->GetChildCount(), 2u);
	EXPECT_NE(dynamic_cast<const ibValueCommandBarItem*>(readSubmenu->GetChild(0)), nullptr);

	ibValueCommandBarSubmenu* const readNested = dynamic_cast<ibValueCommandBarSubmenu*>(readSubmenu->GetChild(1));
	ASSERT_NE(readNested, nullptr);
	ASSERT_EQ(readNested->GetChildCount(), 1u);
	const ibValueCommandBarItem* const deepest = dynamic_cast<const ibValueCommandBarItem*>(readNested->GetChild(0));
	ASSERT_NE(deepest, nullptr);
	EXPECT_EQ(deepest->GetName(), ibString(ibT("Command3")));
	EXPECT_EQ(deepest->GetBar(), read);
}

TEST(CommandBarSubmenu, ASubmenuIsAGroupWhoseEntriesHaveIdsOfTheirOwn)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	const ibValue held = MakeBar();
	ibValueCommandBar* const bar = BarOf(held);
	ASSERT_NE(bar, nullptr);
	ibValueCommandBarSubmenu* const submenu = bar->AddSubmenu();
	submenu->AddSubmenu();

	const std::vector<ibCommandEntry>& entries = bar->BuildCommands();
	ASSERT_EQ(entries.size(), 1u);
	const ibCommandEntry& group = entries[0];
	EXPECT_EQ(group.kind, ibCommandEntryKind_Group);
	EXPECT_EQ(group.item, submenu);
	EXPECT_EQ(group.caption, ibString(ibT("Submenu1")));   // no caption of its own: its name
	ASSERT_EQ(group.members.size(), 1u);

	const ibCommandEntry& nested = group.members[0];
	EXPECT_EQ(nested.kind, ibCommandEntryKind_Group);
	EXPECT_NE(nested.id, group.id);
	EXPECT_EQ(bar->FindEntry(group.id), &group);
	EXPECT_EQ(bar->FindEntry(nested.id), &nested);

	// Hidden, it goes with all it holds.
	ASSERT_TRUE(ibPropertyGate::SetValue(submenu, submenu->GetProperty(ibString(ibT("Visible"))), ibVariant(false)));
	EXPECT_TRUE(bar->BuildCommands().empty());
}

TEST(CommandBarSubmenu, ACommandBoundToNothingRunsNothingGreyedUnderItsName)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	const ibValue held = MakeBar();
	ibValueCommandBar* const bar = BarOf(held);
	ASSERT_NE(bar, nullptr);
	ibValueCommandBarItem* const command = bar->AddCommandItem();
	ibValueCommandBarSubmenu* const submenu = bar->AddSubmenu();
	submenu->AddCommandItem();

	// An entry all the same — the studio draws it, to be bound again; where the form runs it is not drawn.
	const std::vector<ibCommandEntry>& entries = bar->BuildCommands();
	ASSERT_EQ(entries.size(), 2u);
	const ibCommandEntry& unbound = entries[0];
	EXPECT_TRUE(unbound.IsUnbound());
	EXPECT_EQ(unbound.item, command);
	EXPECT_FALSE(unbound.enabled);
	EXPECT_EQ(unbound.caption, ibString(ibT("Command1")));   // no caption of its own: its name

	// …in a submenu the same, the submenu itself being none.
	const ibCommandEntry& group = entries[1];
	EXPECT_FALSE(group.IsUnbound());
	ASSERT_EQ(group.members.size(), 1u);
	EXPECT_TRUE(group.members[0].IsUnbound());
	EXPECT_EQ(group.members[0].caption, ibString(ibT("Command2")));

	// Hidden, it is none.
	ASSERT_TRUE(ibPropertyGate::SetValue(command, command->GetProperty(ibString(ibT("Visible"))), ibVariant(false)));
	const std::vector<ibCommandEntry>& left = bar->BuildCommands();
	ASSERT_EQ(left.size(), 1u);
	EXPECT_EQ(left[0].item, submenu);
}
