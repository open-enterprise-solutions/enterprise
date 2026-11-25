// A STYLE IN THE CORE - the items the platform's look is made of, the platform's own values for both looks, a
// application's style lying over them, and a style as a node: what is written is read back, what an older build
// does not know is left out, a value of another kind is refused.

#include <gtest/gtest.h>

#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/graphics/border.h"
#include "core/graphics/colour.h"
#include "core/graphics/font.h"
#include "core/serialize/dataBuilder.h"
#include "core/style/style.h"
#include "core/style/styleItem.h"

// Every item has a number and a name of its own, and its kind.
TEST(Style, EveryItemHasANumberAndANameOfItsOwn) {
	std::set<int> numbers;
	std::set<std::string> names;
	for (const ibStyleItemInfo& info : ibStyleItemInfo::All()) {
		EXPECT_TRUE(numbers.insert(static_cast<int>(info.m_item)).second) << info.m_name;
		EXPECT_TRUE(names.insert(info.m_name).second) << info.m_name;
		EXPECT_EQ(ibStyleItemInfo::Of(info.m_item), &info);
		EXPECT_EQ(ibStyleItemInfo::Of(ibString(info.m_name)), &info);
	}
	EXPECT_EQ(ibStyleItemInfo::Of(ibString(ibT("NoSuchItem"))), nullptr);
	EXPECT_EQ(ibStyleItemInfo::Of(static_cast<ibStyleItem>(999)), nullptr);

	// The numbers a saved form and the wire hold, every one, written out here: one that moves fails this.
	const std::vector<std::pair<std::string, int>> held = {
		{ "FormBackground", 1 }, { "FormText", 2 }, { "ButtonBackground", 3 }, { "ButtonText", 4 },
		{ "FieldBackground", 5 }, { "FieldText", 6 }, { "Selection", 7 }, { "SelectionText", 8 },
		{ "AlternateBackground", 9 }, { "TooltipBackground", 10 }, { "TooltipText", 11 }, { "SpecialText", 12 },
		{ "NegativeNumber", 13 }, { "Border", 14 }, { "TableHeaderBackground", 15 }, { "TableHeaderText", 16 },
		{ "TableFooterBackground", 17 }, { "TableFooterText", 18 }, { "ReportHeader", 19 }, { "ReportGroup", 20 },
		{ "ReportLine", 21 }, { "Activity", 22 }, { "Accent", 23 }, { "Navigation", 24 },
		{ "SecondaryNavigation", 25 }, { "Important", 26 }, { "DesktopBackground", 27 }, { "PanelBackground", 28 },
		{ "BarBackground", 29 }, { "ControlBackground", 30 }, { "DisabledText", 31 }, { "Caption", 32 },
		{ "CaptionText", 33 }, { "AccentPressed", 34 }, { "AccentText", 35 }, { "Shelf", 36 },
		{ "TextFont", 100 }, { "SmallFont", 101 }, { "RegularFont", 102 }, { "LargeFont", 103 }, { "VeryLargeFont", 104 },
		{ "ControlBorder", 200 }, { "ButtonBorder", 201 },
	};
	ASSERT_EQ(ibStyleItemInfo::All().size(), held.size());
	for (const auto& [name, number] : held) {
		const ibStyleItemInfo* const info = ibStyleItemInfo::Of(ibString(name.c_str()));
		ASSERT_NE(info, nullptr) << name;
		EXPECT_EQ(static_cast<int>(info->m_item), number) << name;
	}
	EXPECT_EQ(ibStyleItemInfo::Of(ibStyleItem::ReportGroup)->m_kind, ibStyleKind::ColourList);
	EXPECT_EQ(ibStyleItemInfo::Of(ibStyleItem::Caption)->m_kind, ibStyleKind::ColourList);
}

// The platform's style has a value of the item's kind for every item, in both looks; its light look keeps the forms'
// colours as they were.
TEST(Style, ThePlatformsHasEveryItemInBothLooks) {
	const ibStyle& platform = ibStyle::Platform();
	for (const ibStyleItemInfo& info : ibStyleItemInfo::All()) {
		for (const ibStyleValues* look : { &platform.m_light, &platform.m_dark }) {
			const ibStyleValue* const value = look->Find(info.m_item);
			ASSERT_NE(value, nullptr) << info.m_name;
			switch (info.m_kind) {
			case ibStyleKind::Colour: EXPECT_TRUE(std::get<ibColour>(*value).IsOk()) << info.m_name; break;
			case ibStyleKind::ColourList: EXPECT_FALSE(std::get<std::vector<ibColour>>(*value).empty()) << info.m_name; break;
			case ibStyleKind::Font: EXPECT_TRUE(std::get<ibFont>(*value).IsOk()) << info.m_name; break;
			case ibStyleKind::Border: EXPECT_NE(std::get<ibBorder>(*value).m_kind, ibBorderKind::None) << info.m_name; break;
			}
		}
	}
	// What the light look keeps: the forms' ink on a ground a step below white, the fields white (frmserver's default
	// style, taken from here; cream until 2026-11-13) and the desktop theme's colours, which it paints with from here
	// (deskgui/univ/theme/lunaTheme.cpp) - each as the screen shows it, the browser's as well.
	const std::vector<std::pair<ibStyleItem, ibColour>> kept = {
		{ ibStyleItem::FormText, ibColour(0x3F, 0x5C, 0x77) },
		{ ibStyleItem::FormBackground, ibColour(0xEE, 0xF2, 0xF6) },
		{ ibStyleItem::FieldBackground, ibColour(0xFF, 0xFF, 0xFF) },
		{ ibStyleItem::FieldText, ibColour(0, 0, 0) },
		{ ibStyleItem::ButtonBackground, ibColour(0xFF, 0xFF, 0xFF) },
		{ ibStyleItem::ButtonText, ibColour(0, 0, 0) },
		{ ibStyleItem::Selection, ibColour(0xD9, 0x77, 0x57) },
		{ ibStyleItem::SelectionText, ibColour(0xFF, 0xFF, 0xFF) },
		{ ibStyleItem::TooltipBackground, ibColour(0xE6, 0xEE, 0xF5) },
		{ ibStyleItem::TooltipText, ibColour(0, 0, 0) },
		{ ibStyleItem::Border, ibColour(0xA8, 0xBA, 0xC8) },
		{ ibStyleItem::TableHeaderBackground, ibColour(0xE6, 0xEE, 0xF5) },
		{ ibStyleItem::TableHeaderText, ibColour(0x3F, 0x5C, 0x77) },
		{ ibStyleItem::Accent, ibColour(0xD9, 0x77, 0x57) },
		{ ibStyleItem::Activity, ibColour(0xD9, 0x77, 0x57) },
		{ ibStyleItem::Navigation, ibColour(0xDD, 0xE4, 0xE8) },
		{ ibStyleItem::SecondaryNavigation, ibColour(0xF5, 0xF7, 0xF8) },
		{ ibStyleItem::DesktopBackground, ibColour(0xD3, 0xDB, 0xE1) },
		{ ibStyleItem::PanelBackground, ibColour(0xE7, 0xEC, 0xF0) },
		{ ibStyleItem::BarBackground, ibColour(0xDD, 0xE4, 0xE8) },
		{ ibStyleItem::ControlBackground, ibColour(0xE6, 0xEE, 0xF5) },
		{ ibStyleItem::DisabledText, ibColour(0x94, 0xA6, 0xB4) },
		{ ibStyleItem::CaptionText, ibColour(0xFF, 0xFF, 0xFF) },
		{ ibStyleItem::AccentPressed, ibColour(0xB8, 0x5A, 0x38) },
		{ ibStyleItem::AccentText, ibColour(0xFF, 0xFF, 0xFF) },
		{ ibStyleItem::Shelf, ibColour(0xE0, 0xD8, 0xCC) },
	};
	for (const auto& [item, colour] : kept)
		EXPECT_EQ(std::get<ibColour>(platform.Get(item, ibStyleLook::Light)), colour) << ibStyleItemInfo::Of(item)->m_name;
	EXPECT_NE(std::get<ibColour>(platform.Get(ibStyleItem::FormBackground, ibStyleLook::Dark)), ibColour(0xEE, 0xF2, 0xF6));
	EXPECT_EQ(std::get<std::vector<ibColour>>(platform.Get(ibStyleItem::ReportGroup, ibStyleLook::Light)).size(), 3u);
	// …an active caption, lit from the top: its two, the light one first
	EXPECT_EQ(std::get<std::vector<ibColour>>(platform.Get(ibStyleItem::Caption, ibStyleLook::Light)),
		(std::vector<ibColour>{ ibColour(0x5A, 0x7B, 0x95), ibColour(0x3F, 0x5C, 0x77) }));
}

// An application's style gives only what it changes: the rest is the platform's, look by look.
TEST(Style, AStyleLiesOverThePlatforms) {
	ibStyle own;
	ASSERT_TRUE(own.m_dark.Set(ibStyleItem::Accent, ibColour(1, 2, 3)));
	EXPECT_EQ(std::get<ibColour>(own.Get(ibStyleItem::Accent, ibStyleLook::Dark)), ibColour(1, 2, 3));
	EXPECT_EQ(own.Get(ibStyleItem::Accent, ibStyleLook::Light), ibStyle::Platform().Get(ibStyleItem::Accent, ibStyleLook::Light));
	EXPECT_EQ(own.Get(ibStyleItem::FormText, ibStyleLook::Dark), ibStyle::Platform().Get(ibStyleItem::FormText, ibStyleLook::Dark));
	own.m_dark.Remove(ibStyleItem::Accent);
	EXPECT_EQ(own.Get(ibStyleItem::Accent, ibStyleLook::Dark), ibStyle::Platform().Get(ibStyleItem::Accent, ibStyleLook::Dark));
}

// An author's colour, chosen in the light look, is itself there; in the dark its lightness is carried from the light
// look's form ground and ink onto the dark look's, its hue kept - within a step of rounding.
TEST(Style, AnAuthorsColourIsCarriedIntoTheDarkLook) {
	const ibStyle& platform = ibStyle::Platform();
	const auto within = [](const ibColour& got, const ibColour& expected) {
		return std::abs(got.m_red - expected.m_red) <= 1 && std::abs(got.m_green - expected.m_green) <= 1
			&& std::abs(got.m_blue - expected.m_blue) <= 1 && got.m_alpha == expected.m_alpha;
	};
	EXPECT_EQ(platform.Get(ibColour(0xFF, 0xFF, 0x00), ibStyleLook::Light), ibColour(0xFF, 0xFF, 0x00));
	EXPECT_FALSE(platform.Get(ibColour(), ibStyleLook::Dark).IsOk());
	const std::vector<std::pair<ibColour, ibColour>> carried = {
		{ ibColour(0xFF, 0xFF, 0xFF), ibColour(26, 26, 26) },       // white: below the dark look's ground
		{ ibColour(0xEE, 0xF2, 0xF6), ibColour(35, 38, 41) },       // the light look's form ground: about the dark's
		{ ibColour(0x3F, 0x5C, 0x77), ibColour(184, 216, 248) },    // the light look's form ink: about the dark's
		{ ibColour(0xFF, 0xFF, 0x00), ibColour(37, 37, 0) },        // yellow: a dark olive
		{ ibColour(0x00, 0x80, 0x00), ibColour(115, 218, 108) },    // dark green ink: a light green
		{ ibColour(0x00, 0x00, 0x00), ibColour(255, 255, 255) },    // black: white
		{ ibColour(0x00, 0x00, 0x80), ibColour(218, 230, 255) },    // navy, darker than the ink: a pale blue, not white
		{ ibColour(0x80, 0x00, 0x00), ibColour(255, 207, 199) },    // maroon: a pale red
		{ ibColour(0xC0, 0x39, 0x2B), ibColour(255, 146, 130) },    // red ink: a light red
	};
	for (const auto& [colour, expected] : carried) {
		const ibColour got = platform.Get(colour, ibStyleLook::Dark);
		EXPECT_TRUE(within(got, expected)) << int(colour.m_red) << ',' << int(colour.m_green) << ',' << int(colour.m_blue)
			<< " -> " << int(got.m_red) << ',' << int(got.m_green) << ',' << int(got.m_blue);
	}
	EXPECT_EQ(platform.Get(ibColour(0xFF, 0xFF, 0xFF, 0x80), ibStyleLook::Dark).m_alpha, 0x80);
}

// An item takes a value of its own kind and no other.
TEST(Style, AValueOfAnotherKindIsRefused) {
	ibStyleValues look;
	EXPECT_FALSE(look.Set(ibStyleItem::FormText, ibBorder(ibBorderKind::Line, 1)));
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, ibColour(1, 2, 3)));
	EXPECT_FALSE(look.Set(ibStyleItem::ReportGroup, ibColour(1, 2, 3)));
	EXPECT_FALSE(look.Set(static_cast<ibStyleItem>(999), ibColour(1, 2, 3)));
	EXPECT_TRUE(look.IsEmpty());
}

// What the text of a style could not hold is not taken, so what is set is what is read back.
TEST(Style, WhatItsTextCouldNotHoldIsRefused) {
	ibStyleValues look;
	EXPECT_FALSE(look.Set(ibStyleItem::Accent, ibColour())) << "no colour";
	EXPECT_FALSE(look.Set(ibStyleItem::Accent, ibColour(1, 2, 3, 128))) << "not opaque";
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, ibFont())) << "no font";
	ibFont half;
	half.m_pointSize = 9.5;
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, half)) << "a fractional size";
	ibFont struck;
	struck.m_pointSize = 9;
	struck.m_strikethrough = true;
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, struck)) << "struck through";
	ibFont light;
	light.m_pointSize = 9;
	light.m_weight = ibFONTWEIGHT_LIGHT;
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, light)) << "a weight its text reads as another";
	ibFont comma;
	comma.m_pointSize = 9;
	comma.m_faceName = ibT("A,B");
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, comma)) << "a comma in the face";
	ibFont huge;
	huge.m_pointSize = 1e12;
	EXPECT_FALSE(look.Set(ibStyleItem::TextFont, huge)) << "a size past kMaxPoints";
	EXPECT_FALSE(look.Set(ibStyleItem::ControlBorder, ibBorder(ibBorderKind::Line, -1))) << "a negative width";
	EXPECT_FALSE(look.Set(ibStyleItem::ControlBorder, ibBorder(ibBorderKind::Line, ibBorder::kMaxWidth + 1)))
		<< "too wide";
	EXPECT_FALSE(look.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{})) << "no level";
	EXPECT_FALSE(look.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{ ibColour(1, 2, 3), ibColour() }))
		<< "a level with no colour";
	EXPECT_TRUE(look.IsEmpty());
}

// A style as a node and back - every kind; and the platform's whole.
TEST(Style, AStyleIsReadBackAsItWasWritten) {
	ibStyle written;
	ibFont large;
	large.m_faceName = ibT("Segoe UI");
	large.m_pointSize = 12;
	large.m_weight = ibFONTWEIGHT_BOLD;
	ASSERT_TRUE(written.m_light.Set(ibStyleItem::Accent, ibColour(10, 20, 30)));
	ASSERT_TRUE(written.m_light.Set(ibStyleItem::ReportGroup,
		std::vector<ibColour>{ ibColour(1, 1, 1), ibColour(2, 2, 2) }));
	ASSERT_TRUE(written.m_light.Set(ibStyleItem::LargeFont, large));
	ASSERT_TRUE(written.m_dark.Set(ibStyleItem::ButtonBorder, ibBorder(ibBorderKind::Sunken, 2)));
	ibDataValue node;
	ASSERT_TRUE(written.WriteNode(node));
	ibStyle read;
	ASSERT_TRUE(read.ReadNode(node));
	EXPECT_EQ(read, written);

	ibDataValue platformNode;
	ASSERT_TRUE(ibStyle::Platform().WriteNode(platformNode));
	ibStyle platform;
	ASSERT_TRUE(platform.ReadNode(platformNode));
	EXPECT_EQ(platform, ibStyle::Platform());
}

// What a later build wrote and this one does not know - an item it has not, a value of another kind, a text that names
// no value - is left out, and the rest is read.
TEST(Style, WhatIsNotKnownIsLeftOutAndTheRestRead) {
	auto look = std::make_shared<ibDataNode>();
	look->AddField(ibT("Accent"), ibDataValue::String(ibT("10,20,30")));
	look->AddField(ibT("SomethingLater"), ibDataValue::String(ibT("1,2,3")));
	look->AddField(ibT("FormText"), ibDataValue::String(ibT("not a colour")));
	look->AddField(ibT("ControlBorder"), ibDataValue::String(ibT("9,1")));
	look->AddField(ibT("ReportGroup"), ibDataValue::String(ibT("1,2,3")));
	ibStyleValues read;
	ASSERT_TRUE(read.ReadNode(ibDataValue::Child(look)));
	ASSERT_NE(read.Find(ibStyleItem::Accent), nullptr);
	EXPECT_EQ(std::get<ibColour>(*read.Find(ibStyleItem::Accent)), ibColour(10, 20, 30));
	EXPECT_EQ(read.Find(ibStyleItem::FormText), nullptr);
	EXPECT_EQ(read.Find(ibStyleItem::ControlBorder), nullptr);
	EXPECT_EQ(read.Find(ibStyleItem::ReportGroup), nullptr);

	// An empty list is no list: left out.
	auto empty = std::make_shared<ibDataNode>();
	empty->AddField(ibT("ReportGroup"), ibDataValue::Array(std::vector<ibDataValue>{}));
	ibStyleValues none;
	ASSERT_TRUE(none.ReadNode(ibDataValue::Child(empty)));
	EXPECT_EQ(none.Find(ibStyleItem::ReportGroup), nullptr);

	// A list with one text that names no colour is left out whole, not read short.
	auto list = std::make_shared<ibDataNode>();
	list->AddField(ibT("ReportGroup"), ibDataValue::Array(std::vector<ibDataValue>{
		ibDataValue::String(ibT("1,2,3")), ibDataValue::String(ibT("no colour")) }));
	ibStyleValues partly;
	ASSERT_TRUE(partly.ReadNode(ibDataValue::Child(list)));
	EXPECT_EQ(partly.Find(ibStyleItem::ReportGroup), nullptr);
}

// A border as text: its kind's number and its width; anything else is refused and the border left as it was.
TEST(Style, ABorderIsItsKindAndItsWidth) {
	ibBorder border;
	ASSERT_TRUE(border.FromString(ibT("4,2")));
	EXPECT_EQ(border, ibBorder(ibBorderKind::Rounded, 2));
	EXPECT_EQ(border.ToString(), ibString(ibT("4,2")));
	ASSERT_TRUE(border.FromString(ibT(" 1 , 3 ")));
	EXPECT_EQ(border, ibBorder(ibBorderKind::Line, 3)) << "spaces around the numbers";
	ASSERT_TRUE(border.FromString(ibT("4,2")));
	for (const ibChar* bad : { ibT(""), ibT("4"), ibT("9,1"), ibT("-1,1"), ibT("1,-1"), ibT("a,b"), ibT("1,2,3"),
		ibT("1,65"), ibT("1,3000000000") }) {
		EXPECT_FALSE(border.FromString(bad)) << ibString(bad).ToUtf8();
		EXPECT_EQ(border, ibBorder(ibBorderKind::Rounded, 2)) << ibString(bad).ToUtf8();
	}
}
