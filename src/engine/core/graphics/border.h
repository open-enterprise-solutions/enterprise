#ifndef _CORE_BORDER_H__
#define _CORE_BORDER_H__

////////////////////////////////////////////////////////////////////////////
// ibBorder — A BORDER AS DATA: how it is drawn and how wide. Its colour is not its own: a style's Border item
// gives it, so a theme recolours every border at once.
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fstring.h"

#include <cstdint>

// How a border is drawn. The numbers are what its text holds; a new kind is appended.
enum class ibBorderKind : std::uint8_t {
	None = 0,      // no border
	Line = 1,      // a plain line
	Raised = 2,    // lit from above: the control stands out
	Sunken = 3,    // lit from below: the control is set in
	Rounded = 4,   // a line with rounded corners
};

struct CORE_API ibBorder {
	ibBorderKind m_kind = ibBorderKind::None;
	int          m_width = 0;   // in device-independent pixels; 0 with a kind other than None is drawn 1 wide

	// The widest a border is drawn, in device-independent pixels.
	static constexpr int kMaxWidth = 64;

	ibBorder() = default;
	ibBorder(ibBorderKind kind, int width) : m_kind(kind), m_width(width) {}

	// A text — `kind,width`, the kind by its number, e.g. "1,1" for a line one pixel wide. A text that is not two
	// whole numbers, or names a kind there is none of, or a width outside 0..kMaxWidth, is refused and the border left
	// alone.
	bool FromString(const ibString& text);
	ibString ToString() const;

	bool operator==(const ibBorder& other) const { return m_kind == other.m_kind && m_width == other.m_width; }
};

#endif
