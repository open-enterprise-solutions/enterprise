#ifndef __INPUT_MASK_H__
#define __INPUT_MASK_H__

////////////////////////////////////////////////////////////////////////////
// The INPUT MASK — `+1 (000) 000-00-00;0;_` — what a field lets you type.
////////////////////////////////////////////////////////////////////////////
//
// The format (formatString.h) says how a value is SHOWN; the mask says what can be TYPED: a position takes a
// character of its class or nothing, a literal stays where it stands. Delphi's EditMask in three sections — the
// mask; whether the literals belong to the value's text (`1`, the default) or not (`0`); the character an empty
// position shows (a space when none is named: the mask stays hidden behind the value). docs/private/input-mask.md.
//
// The MASKED TEXT is a field's text while it is edited: one character per slot of the mask — the literal where
// the mask has one, a typed character or the blank where it has a position. Replace is the one verb an editor
// calls for a keystroke, a deletion and a paste alike, and the browser's controls/inputMask.js transcribes it.
//
// ⚠ `!` IS NOT DELPHI'S. There it only decides where unfilled positions are stored; here, written first, it says
// the mask after it is a VALUE'S SHAPE rather than positions (Max, 2026-11-23):
//  - digits with a point — `!#9999999.999` — a NUMBER, typed as a number is: no positions and no blanks, the digits
//    where the caret stands, no more of them before the point and after it than the mask has `9`s, `.` or `,` the
//    point, `-` the sign where a `#` leads;
//  - a date pattern — `!dd.MM.yyyy HH:mm:ss` — a DATE: its letters are digit positions, and a digit that makes no day,
//    month, hour, minute or second is not taken (a day past 31, a month past 12); the time beside a date may be left
//    out. Which day a month has is the server's to say, reading the value.
// Anything else after a `!` is read as positions, the `!` passed over.
//
////////////////////////////////////////////////////////////////////////////

#include <optional>
#include <vector>

#include "core/formatString.h"   // ibFormatString::PresetPattern — a generated date mask is the generated format's pattern

class CORE_API ibInputMask {
public:

	bool IsEmpty() const { return m_slots.empty() && !m_number; }

	// The text as it was written — Parse's argument, given back unchanged.
	ibString Render() const { return m_text; }

	// A text laid into the mask. One already in the mask's shape stays as it is; any other gives its characters to
	// the positions in order, as typing them would — a literal typed steps past its place, a blank takes none, a
	// character no position takes is dropped. The empty text gives the SKELETON: the literals and the blanks.
	// What an editor starts from, and what a field with nothing in it shows. A number's is the number's own text:
	// `1 234,500` is `1234.500`, digits past the shape dropped; the empty text is empty.
	ibString Apply(const ibString& text) const;

	// ⭐ THE EDIT — the editor's own Replace(from, to, text), done to the masked text: the positions in [from, to)
	// are blanked (a literal is never removed), and `text` is typed from `from` on. A deletion that covers literals
	// only reaches the nearest position on the side the caret leaves: before the range when the caret stands at its
	// end (Backspace), after it otherwise (Delete). A number's range is removed as text is, unless what is left is no
	// number of its shape. `caret` is where the caret stands, and where it goes. A text not in the mask's shape is laid
	// into it first. False when nothing changed — the key the mask does not take.
	bool Replace(ibString& masked, size_t from, size_t to, const ibString& text, size_t& caret) const;

	// The value's text out of a masked one: the blanks dropped, the literals kept or dropped as the second section
	// says — none after the last character typed (`23.11.2026` out of `23.11.2026   :  :  `). A wholly blank text is
	// the empty value — the field was cleared. False, with the reason, when a required position is blank in a text that
	// is not. A number's is its text, a whole part written where none was typed (`.5` is `0.5`), a point with nothing
	// after it dropped; one with no digit is the empty value.
	bool Strip(const ibString& masked, ibString& text, ibString* refusal = nullptr) const;

	// THE RUN AT `position` of a masked text — the positions between two literals that the caret there stands in or
	// just after: a day, a month, an hour; a number's whole part or its fraction. What a double click selects, from
	// `first` to `last` both included. False where there is none: no mask, a literal on both sides, an empty part.
	bool GetRun(const ibString& masked, size_t position, size_t& first, size_t& last) const;

	bool operator == (const ibInputMask& other) const { return m_text == other.m_text; }

	// The text read: `mask[;literals[;blank]]`. Never fails: a character that is no position is a literal.
	static ibInputMask Parse(const ibString& text);

	// ⭐ THE MASK A TYPE DESCRIPTION GIVES, as ibFormatString::FromTypeDesc gives the format — for a type of ONE
	// kind. A number: its shape — as many digits before the point as its type keeps, a sign unless the type is
	// NonNegative, as many after it as the type keeps: `Number(10,3)` gives `!#9999999.999`. The point is always `.`:
	// it is what the number is read back with (ibNumber knows no other), and `,` typed is the point as well. A date:
	// the generated format's pattern — `!dd.MM.yyyy`, `!HH:mm:ss`, `!dd.MM.yyyy HH:mm:ss`; the time beside a date may
	// be left out (a document's date typed alone is its midnight, as before). False for a number nobody bounded
	// (precision 0) and for any other type, a composite one among them: no mask. The description is its owner's.
	template <class TypeDesc>
	static bool FromTypeDesc(const TypeDesc& type, ibInputMask& mask) {
		if (type.GetClsidCount() != 1)
			return false;

		if (type.ContainType(ibValueTypes::TYPE_NUMBER)) {
			if (type.GetPrecision() == 0)
				return false;
			mask = Parse(NumberMask(type.GetPrecision() - type.GetScale(), type.GetScale(), !type.IsNonNegative()));
			return true;
		}

		if (type.ContainType(ibValueTypes::TYPE_DATE)) {
			ibDatePreset preset = ibDatePreset::DateTime;
			switch (type.GetDateFraction()) {
			case ibDateFractions::ibDateFractions_Date:
				preset = ibDatePreset::Date;
				break;
			case ibDateFractions::ibDateFractions_Time:
				preset = ibDatePreset::Time;
				break;
			default:
				break;
			}
			mask = Parse(ibString(ibT("!")) + ibFormatString::PresetPattern(preset));
			return true;
		}

		return false;
	}

private:

	enum class ibSlotKind : unsigned char {
		Literal,
		Digit,           // 0 9, a date's letters
		DigitOrSign,     // #
		Letter,          // L l
		LetterOrDigit,   // A a
		Any,             // C c
	};

	enum class ibSlotCase : unsigned char {
		AsTyped,
		Upper,           // >
		Lower,           // <
	};

	// What of a date a digit position is — its letter in the pattern.
	enum class ibDatePart : unsigned char {
		None,
		Day,             // d
		Month,           // M
		Year,            // y
		Hour,            // H
		Hour12,          // h
		Minute,          // m
		Second,          // s
	};

	struct ibSlot {
		ibSlotKind m_kind = ibSlotKind::Literal;
		ibChar     m_literal = 0;          // a literal's character
		bool       m_required = false;     // 0 L A C, a date's
		ibSlotCase m_case = ibSlotCase::AsTyped;
		ibDatePart m_part = ibDatePart::None;
	};

	// A NUMBER'S SHAPE — what `!` before digits and a point makes of the mask: its text is the number's own.
	struct ibNumberShape {
		size_t m_whole = 0;        // the digits before the point, at most
		size_t m_fraction = 0;     // …and after it
		bool   m_signed = false;   // a `#` leads: `-` may be typed

		// A text made the number's: a sign, the digits, the point — what does not fit dropped.
		ibString Apply(const ibString& text) const;
		// One character typed at the caret: a digit where it stands, the point, the sign.
		bool Type(ibString& text, size_t& caret, ibChar c) const;
	};

	bool IsLiteral(size_t slot) const { return m_slots[slot].m_kind == ibSlotKind::Literal; }
	bool Accepts(size_t slot, ibChar c) const;
	ibChar Cased(size_t slot, ibChar c) const;
	// The positions next to each other that `slot` stands among.
	void GetRun(size_t slot, size_t& first, size_t& last) const;
	// A text of the mask's length whose every character its slot holds.
	bool Fits(const ibString& text) const;
	// One character typed at the caret.
	bool Type(ibString& masked, size_t& caret, ibChar c) const;
	// The date part `slot` belongs to, the digit just put there: false when no value of the part begins so (a day
	// `4_`, a month `13`); the digits after it that make it impossible are blanked — `15` typed over with `3` is `3_`.
	bool TakesPart(ibString& masked, size_t slot) const;
	// The positions in [from, to) emptied.
	void Blank(ibString& masked, size_t from, size_t to) const;

	// The text FromTypeDesc parses for a number.
	static ibString NumberMask(int whole, int fraction, bool withSign);
	// A literal the character typed steps past: itself — and the point and the comma for each other, the two keys a
	// decimal point is typed with.
	static bool StepsPast(ibChar literal, ibChar c);

	std::vector<ibSlot> m_slots;
	std::optional<ibNumberShape> m_number;   // a number's mask — none: positions
	ibString m_text;                          // as written
	bool     m_keepLiterals = true;           // the second section
	ibChar   m_blank = ibT(' ');              // the third
};

#endif
