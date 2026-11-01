#ifndef _CORE_STYLE_H__
#define _CORE_STYLE_H__

////////////////////////////////////////////////////////////////////////////
// A STYLE — the values the platform's look is painted with: one set for the light look, one for the dark, over the
// same items (styleItem.h). The platform's own is the default (ibStyle::Platform); a configuration's style lies over
// it, giving only what it changes - an item it leaves out is the platform's. The person's theme says which of the two
// sets is in force.
////////////////////////////////////////////////////////////////////////////

#include "core/graphics/border.h"
#include "core/graphics/colour.h"
#include "core/graphics/font.h"
#include "core/serialize/dataBuilder.h"
#include "core/style/styleItem.h"

#include <map>
#include <variant>
#include <vector>

// An item's value, of the item's kind: a colour, a list of colours (ReportGroup), a font or a border.
using ibStyleValue = std::variant<ibColour, std::vector<ibColour>, ibFont, ibBorder>;

// The values of one look: an item at most once, each of its own kind.
class CORE_API ibStyleValues {
public:

	// This set's value of the item - null when it gives none.
	const ibStyleValue* Find(ibStyleItem item) const;

	// The item given that value - refused, nothing changed, when the item is none of the list's, the value is not of
	// its kind, or its text does not read back as it: a colour unset or not opaque, a font unset, larger than
	// kMaxPoints, of a fractional size, struck through, of a weight or a face its text changes, a border wider than
	// ibBorder::kMaxWidth, a list of colours empty or with one of those in it. What is set is what is read back.
	bool Set(ibStyleItem item, const ibStyleValue& value);

	// …or given none: the platform's, in a configuration's style.
	void Remove(ibStyleItem item) { m_values.erase(item); }

	bool IsEmpty() const { return m_values.empty(); }

	// The largest font a style holds, in points.
	static constexpr double kMaxPoints = 1000;

	// A child node, a field an item, keyed by its name and holding its text: `r,g,b`, a font's text, a border's
	// `kind,width`, and an array of texts for a list. An item read that this build has not, or of another kind, or a
	// text that names no value Set would take, is left out - an older build reads a later one's style, less what it
	// does not know. An item written twice is read as the last of them that reads.
	bool ReadNode(const ibDataValue& value);
	bool WriteNode(ibDataValue& value) const;

	bool operator==(const ibStyleValues& other) const { return m_values == other.m_values; }

private:
	std::map<ibStyleItem, ibStyleValue> m_values;
};

class CORE_API ibStyle {
public:

	// The value of the item in the look asked for: this style's, else the platform's. Every item of the list has one;
	// an item that is none of the list's, an unset colour. The reference holds until this style's set changes; the
	// platform's, for good.
	const ibStyleValue& Get(ibStyleItem item, bool dark) const;

	// THE PLATFORM'S STYLE - a value for every item in both looks; the default of every other. Never torn down: a
	// client may ask it while the process ends.
	static const ibStyle& Platform();

	// A child node with the two sets, Light and Dark.
	bool ReadNode(const ibDataValue& value);
	bool WriteNode(ibDataValue& value) const;

	bool operator==(const ibStyle& other) const { return m_light == other.m_light && m_dark == other.m_dark; }

	ibStyleValues m_light;
	ibStyleValues m_dark;
};

#endif
