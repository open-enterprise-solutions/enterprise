// A BUTTON AS A TILE - the representation a finger presses: the picture large over the caption and a note under
// them, the button filling what its sizer gives it. What the server says of it: the member of Representation, the
// note saved with the button, and the state a client draws from - the note going with a tile alone.

#include <gtest/gtest.h>

#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/controlEnum.h"
#include "frmserver/visualView/ctrl/tableBox.h"   // ibValueModelTableBoxColumn - no tile of its own
#include "frmserver/visualView/ctrl/widgets.h"
#include "protocol/protocol.h"

namespace {

ibString Text(const ibDataNode& state, const char* name)
{
	const auto value = state.FindField(name);
	return value ? value->AsString() : ibString();
}

int Number(const ibDataNode& state, const char* name)
{
	const auto value = state.FindField(name);
	return value ? static_cast<int>(value->AsInt()) : -1;
}

// A button given its representation the way a saved form gives it: through its node.
void Represent(ibValueButton& button, ibRepresentation representation)
{
	ibDataNode saved;
	ASSERT_TRUE(button.WriteData(saved));
	saved.SetField(ibT("Representation"), ibDataValue::Int(static_cast<int>(representation)));   // replaces; SetProperty would add a second
	ASSERT_TRUE(button.ReadData(saved));
}

} // namespace

// The fifth member, after the four a button always had, under the number the client reads it by.
TEST(ButtonTile, TileIsAMemberOfRepresentationUnderItsNumber) {
	ibValueEnumRepresentation owner;
	owner.CreateEnumeration();
	ASSERT_EQ(owner.GetEnumCount(), 5u);
	EXPECT_EQ(owner.GetEnumName(4), ibString(ibT("Tile")));
	EXPECT_EQ(static_cast<int>(owner.GetEnumValue(4)), 4);
	EXPECT_EQ(static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::Tile), static_cast<int>(ibRepresentation_Tile));
}

// Saved with the button, as its title is, and read back by one made anew.
TEST(ButtonTile, TheNoteIsSavedWithTheButtonAndReadBack) {
	ibValueButton written;
	written.SetCaption(ibT("Tea"));
	written.SetNote(ibT("12 pcs"));
	Represent(written, ibRepresentation_Tile);
	ibDataNode node;
	ASSERT_TRUE(written.WriteData(node));

	ibValueButton read;
	ASSERT_TRUE(read.ReadData(node));
	EXPECT_EQ(read.GetCaption(), ibString(ibT("Tea")));
	EXPECT_EQ(read.GetNote(), ibString(ibT("12 pcs")));
	ibDataNode state;
	read.OnUpdate(state, nullptr);
	EXPECT_EQ(Number(state, ibProtocol::ibForm::ibState::Representation), 4);

	// A form saved before there was a note - its node has no Note field at all - reads back with none.
	ibDataNode older;
	for (const auto& [name, value] : node.Fields())
		if (name != ibT("Note"))
			older.AddField(name, value);
	ibValueButton old;
	ASSERT_TRUE(old.ReadData(older));
	EXPECT_EQ(old.GetCaption(), ibString(ibT("Tea")));
	EXPECT_EQ(old.GetNote(), ibString());
}

// The state a client draws from: a tile carries its note; any other representation shows none, so carries none.
TEST(ButtonTile, TheStateCarriesTheNoteForATileAlone) {
	ibValueButton button;
	button.SetCaption(ibT("Tea"));
	button.SetNote(ibT("12 pcs"));
	Represent(button, ibRepresentation_Tile);
	ibDataNode tile;
	button.OnUpdate(tile, nullptr);
	EXPECT_EQ(Number(tile, ibProtocol::ibForm::ibState::Representation), static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::Tile));
	EXPECT_EQ(Text(tile, ibProtocol::ibForm::ibState::Caption), ibString(ibT("Tea")));
	EXPECT_EQ(Text(tile, ibProtocol::ibForm::ibState::Note), ibString(ibT("12 pcs")));

	Represent(button, ibRepresentation_PictureAndText);
	ibDataNode plain;
	button.OnUpdate(plain, nullptr);
	EXPECT_EQ(Number(plain, ibProtocol::ibForm::ibState::Representation), static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::PictureAndText));
	EXPECT_FALSE(plain.FindField(ibProtocol::ibForm::ibState::Note).has_value()) << "a note is a tile's";

	// Auto stays what it was: a button with nothing bound shows picture and text, not a tile.
	Represent(button, ibRepresentation_Auto);
	ibDataNode automatic;
	button.OnUpdate(automatic, nullptr);
	EXPECT_EQ(Number(automatic, ibProtocol::ibForm::ibState::Representation), static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::PictureAndText));
}

// The enumeration is shared, and a column has no tile to draw: given one, it shows picture and text - and says so to
// the client, which would otherwise read a number it has no drawing for.
TEST(ButtonTile, AColumnGivenTileShowsPictureAndText) {
	ibDataNode saved;
	saved.SetProperty(ibT("Representation"), ibDataValue::Int(static_cast<int>(ibRepresentation_Tile)));
	ibValueModelTableBoxColumn column;
	ASSERT_TRUE(column.ReadData(saved));
	ibDataNode state;
	column.OnUpdate(state, nullptr);
	EXPECT_EQ(Number(state, ibProtocol::ibForm::ibState::Representation), static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::PictureAndText));
}
