#ifndef _CORE_STYLE_ITEM_H__
#define _CORE_STYLE_ITEM_H__

////////////////////////////////////////////////////////////////////////////
// THE ITEMS OF A STYLE — what the platform's look is made of: the colours, the fonts and the borders, for the server
// and both clients to paint with. The list is the platform's and one: a style gives values to these items, and a
// control's colour is to name one of them for a client to resolve through the values in force.
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fstring.h"

#include <cstdint>
#include <span>

// An item by its number. The numbers are kept forever - a saved form and the wire hold them, so each is written here
// and none is ever moved; a new item takes the next number of its kind's run.
enum class ibStyleItem : std::uint16_t {
	// colours
	FormBackground = 1,
	FormText = 2,
	ButtonBackground = 3,
	ButtonText = 4,
	FieldBackground = 5,           // what a person edits in
	FieldText = 6,
	Selection = 7,                 // the selected row, the selected text
	SelectionText = 8,
	AlternateBackground = 9,       // every other row of a table
	TooltipBackground = 10,
	TooltipText = 11,
	SpecialText = 12,              // a word that stands out: a link, a hint
	NegativeNumber = 13,
	Border = 14,                   // what every border is drawn in
	TableHeaderBackground = 15,
	TableHeaderText = 16,
	TableFooterBackground = 17,
	TableFooterText = 18,
	ReportHeader = 19,
	ReportGroup = 20,              // a list: the grouping levels of a report, the first one outermost
	ReportLine = 21,
	Activity = 22,                 // something under way: a progress bar, a busy mark
	Accent = 23,                   // what the eye is led to: the default button, the current tab
	Navigation = 24,               // the sections panel
	SecondaryNavigation = 25,      // the functions panel of a section
	Important = 26,                // a warning, an error, a required field
	// fonts
	TextFont = 100,                // a control's own text
	SmallFont = 101,
	RegularFont = 102,
	LargeFont = 103,
	VeryLargeFont = 104,
	// borders
	ControlBorder = 200,
	ButtonBorder = 201,
};

// What an item's value is.
enum class ibStyleKind : std::uint8_t {
	Colour,
	ColourList,   // ReportGroup: as many colours as a style gives
	Font,
	Border,
};

// An item, its kind and its name - what the designer and a script call it, and a saved style keys it by.
struct CORE_API ibStyleItemInfo {
	ibStyleItem m_item;
	ibStyleKind m_kind;
	const char* m_name;

	// Every item, in their numbers' order.
	static std::span<const ibStyleItemInfo> All();

	// The item of that number or that name - null for none: a number or a name a later build added, or a typo. A name
	// is matched as written, case and all, as a saved style keys it.
	static const ibStyleItemInfo* Of(ibStyleItem item);
	static const ibStyleItemInfo* Of(const ibString& name);
};

#endif
