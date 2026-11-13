#ifndef _CORE_GEOMETRY_H__
#define _CORE_GEOMETRY_H__

////////////////////////////////////////////////////////////////////////////
// ibPoint, ibSize, ibRect — WHERE AND HOW BIG, as either side keeps it: wx's own (wxPoint, wxSize, wxRect,
// wx/gdicmn.h — the GUI library, which the core does not link), copied field for field.
////////////////////////////////////////////////////////////////////////////
//
// ⭐ WHAT wx HOLDS: the same ints in the same order, no base, nothing virtual. A side's converter moves
// them to wx and back field by field, as it does a colour (ColourToWxColour); the core does not see
// wx/gdicmn.h.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fstring.h"

struct CORE_API ibPoint {
	int m_x = 0;
	int m_y = 0;

	constexpr ibPoint() = default;
	constexpr ibPoint(int x, int y) : m_x(x), m_y(y) {}

	// A text — the form points were always written in (the sides' typeConv PointToString): `x,y`, e.g. "10,20".
	// Refused — an empty text, or one that is not two whole numbers — the point is left alone.
	bool FromString(const ibString& text);

	// …and the text it reads from.
	ibString ToString() const;

	bool operator==(const ibPoint& other) const { return m_x == other.m_x && m_y == other.m_y; }
};

struct CORE_API ibSize {
	int m_width = 0;
	int m_height = 0;

	constexpr ibSize() = default;
	constexpr ibSize(int width, int height) : m_width(width), m_height(height) {}

	// A text — `width,height`, as a point's.
	bool FromString(const ibString& text);
	ibString ToString() const;

	bool operator==(const ibSize& other) const { return m_width == other.m_width && m_height == other.m_height; }
};

// No position, no size said — wx's wxDefaultPosition and wxDefaultSize, whose numbers they are.
constexpr ibPoint ibDefaultPosition(-1, -1);
constexpr ibSize ibDefaultSize(-1, -1);

struct ibRect {
	int m_x = 0;
	int m_y = 0;
	int m_width = 0;
	int m_height = 0;

	ibRect() = default;
	ibRect(int x, int y, int width, int height) : m_x(x), m_y(y), m_width(width), m_height(height) {}
	ibRect(const ibPoint& position, const ibSize& size)
		: m_x(position.m_x), m_y(position.m_y), m_width(size.m_width), m_height(size.m_height) {}

	ibPoint GetPosition() const { return ibPoint(m_x, m_y); }
	ibSize GetSize() const { return ibSize(m_width, m_height); }

	bool operator==(const ibRect& other) const {
		return m_x == other.m_x && m_y == other.m_y && m_width == other.m_width && m_height == other.m_height;
	}
};

// An alignment, in wx's numbers (wxAlignment: a horizontal flag and a vertical one combine), so a side hands it to wx
// as it is. Default (wxALIGN_INVALID) is "not said here": the column's own, the driver's own.
enum ibAlignment : int {
	ibAlignment_Default = -1,                // wxALIGN_INVALID
	ibAlignment_Left = 0x0000,               // wxALIGN_LEFT
	ibAlignment_CenterHorizontal = 0x0100,   // wxALIGN_CENTER_HORIZONTAL
	ibAlignment_Right = 0x0200,              // wxALIGN_RIGHT
	ibAlignment_Top = 0x0000,                // wxALIGN_TOP
	ibAlignment_Bottom = 0x0400,             // wxALIGN_BOTTOM
	ibAlignment_CenterVertical = 0x0800,     // wxALIGN_CENTER_VERTICAL
	ibAlignment_Center = 0x0900              // wxALIGN_CENTER - both
};

// The direction a sizer lays its controls in, in wx's numbers (wxOrientation).
enum ibOrientation : int {
	ibOrientation_Horizontal = 0x0004,   // wxHORIZONTAL
	ibOrientation_Vertical = 0x0008      // wxVERTICAL
};

// A control's size across its sizer's direction, in wx's numbers (wxStretch).
enum ibStretch : int {
	ibStretch_Shrink = 0x1000,   // wxSHRINK
	ibStretch_Expand = 0x2000    // wxEXPAND
};

// The sides a margin is kept on, in wx's numbers (wxDirection) - flags that combine.
enum ibDirection : int {
	ibDirection_Left = 0x0010,    // wxLEFT
	ibDirection_Right = 0x0020,   // wxRIGHT
	ibDirection_Up = 0x0040,      // wxUP
	ibDirection_Down = 0x0080,    // wxDOWN
	ibDirection_All = 0x00f0      // wxALL
};

#endif
