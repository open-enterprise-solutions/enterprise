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
	bool operator!=(const ibPoint& other) const { return !(*this == other); }
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
	bool operator!=(const ibSize& other) const { return !(*this == other); }
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
	bool operator!=(const ibRect& other) const { return !(*this == other); }
};

#endif
