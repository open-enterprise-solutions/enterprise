////////////////////////////////////////////////////////////////////////////
//	Description : The input mask (inputMask.h)
////////////////////////////////////////////////////////////////////////////

#include "core/inputMask.h"
#include "core/localization/dictionary.h"   // _() — the refusal Strip gives

#include <algorithm>

namespace {

bool IsDecimalDigit(ibChar c)
{
	return c >= ibT('0') && c <= ibT('9');
}

// The two keys a decimal point is typed with.
bool IsPoint(ibChar c)
{
	return c == ibT('.') || c == ibT(',');
}

// The digits of a date pattern and what of the date each is (formatString.cpp's DateLetter, the digits of them).
bool IsDateLetter(ibChar c)
{
	switch (c) {
	case ibT('d'): case ibT('M'): case ibT('y'): case ibT('H'): case ibT('h'): case ibT('m'): case ibT('s'):
		return true;
	}
	return false;
}

// A number's shape after the `!`: a `#` or none, `9`s, then a point and `9`s or none. False for anything else.
bool ReadNumberShape(const ibString& shape, size_t& whole, size_t& fraction, bool& withSign)
{
	size_t i = 0;
	withSign = i < shape.length() && shape[i] == ibT('#');
	if (withSign)
		++i;
	whole = 0;
	while (i < shape.length() && shape[i] == ibT('9')) {
		++whole;
		++i;
	}
	fraction = 0;
	if (i < shape.length() && shape[i] == ibT('.')) {
		++i;
		while (i < shape.length() && shape[i] == ibT('9')) {
			++fraction;
			++i;
		}
		if (fraction == 0)
			return false;
	}
	return whole > 0 && i == shape.length();
}

} // namespace

ibInputMask ibInputMask::Parse(const ibString& text)
{
	ibInputMask mask;
	mask.m_text = text;

	const size_t length = text.length();

	// THE MASK — up to the first `;` no `\` fences.
	size_t end = 0;
	for (; end < length && text[end] != ibT(';'); ++end) {
		if (text[end] == ibT('\\'))
			++end;
	}
	end = std::min(end, length);
	const ibString section = text.Mid(0, end);

	// `!` first: the shape of a value. A number's is no positions at all.
	bool datePattern = false;
	size_t from = 0;
	if (!section.IsEmpty() && section[0] == ibT('!')) {
		from = 1;
		ibNumberShape number;
		if (ReadNumberShape(section.Mid(1), number.m_whole, number.m_fraction, number.m_signed)) {
			mask.m_number = number;
			from = end;
		}
		else {
			for (size_t i = 1; i < section.length() && !datePattern; ++i)
				datePattern = IsDateLetter(section[i]);
		}
	}

	// A date's time beside its date may be left out; a time alone may not.
	bool hasDate = false;
	for (size_t i = from; datePattern && i < section.length() && !hasDate; ++i)
		hasDate = section[i] == ibT('y') || section[i] == ibT('M') || section[i] == ibT('d');

	ibSlotCase slotCase = ibSlotCase::AsTyped;
	const auto addLiteral = [&](ibChar c) {
		ibSlot slot;
		slot.m_literal = c;
		mask.m_slots.push_back(slot);
	};
	const auto addPosition = [&](ibSlotKind kind, bool required, ibDatePart part) {
		ibSlot slot;
		slot.m_kind = kind;
		slot.m_required = required;
		slot.m_case = slotCase;
		slot.m_part = part;
		mask.m_slots.push_back(slot);
	};

	for (size_t i = from; i < section.length(); ++i) {
		const ibChar c = section[i];
		if (c == ibT('\\')) {
			if (i + 1 < section.length())
				addLiteral(section[++i]);
			continue;
		}
		if (datePattern) {
			switch (c) {
			case ibT('d'): addPosition(ibSlotKind::Digit, true, ibDatePart::Day); break;
			case ibT('M'): addPosition(ibSlotKind::Digit, true, ibDatePart::Month); break;
			case ibT('y'): addPosition(ibSlotKind::Digit, true, ibDatePart::Year); break;
			case ibT('H'): addPosition(ibSlotKind::Digit, !hasDate, ibDatePart::Hour); break;
			case ibT('h'): addPosition(ibSlotKind::Digit, !hasDate, ibDatePart::Hour12); break;
			case ibT('m'): addPosition(ibSlotKind::Digit, !hasDate, ibDatePart::Minute); break;
			case ibT('s'): addPosition(ibSlotKind::Digit, !hasDate, ibDatePart::Second); break;
			default: addLiteral(c); break;
			}
			continue;
		}
		switch (c) {
		case ibT('!'): break;   // not first: Delphi's, which is not ours
		case ibT('>'): slotCase = ibSlotCase::Upper; break;
		case ibT('<'):
			if (i + 1 < section.length() && section[i + 1] == ibT('>')) {
				++i;
				slotCase = ibSlotCase::AsTyped;
			}
			else
				slotCase = ibSlotCase::Lower;
			break;
		case ibT('0'): addPosition(ibSlotKind::Digit, true, ibDatePart::None); break;
		case ibT('9'): addPosition(ibSlotKind::Digit, false, ibDatePart::None); break;
		case ibT('#'): addPosition(ibSlotKind::DigitOrSign, false, ibDatePart::None); break;
		case ibT('L'): addPosition(ibSlotKind::Letter, true, ibDatePart::None); break;
		case ibT('l'): addPosition(ibSlotKind::Letter, false, ibDatePart::None); break;
		case ibT('A'): addPosition(ibSlotKind::LetterOrDigit, true, ibDatePart::None); break;
		case ibT('a'): addPosition(ibSlotKind::LetterOrDigit, false, ibDatePart::None); break;
		case ibT('C'): addPosition(ibSlotKind::Any, true, ibDatePart::None); break;
		case ibT('c'): addPosition(ibSlotKind::Any, false, ibDatePart::None); break;
		default: addLiteral(c); break;
		}
	}

	// THE LITERALS — `0` leaves them out of the value's text; anything else, or nothing, keeps them.
	const size_t literalsFrom = std::min(end + 1, length);
	const size_t literalsEnd = std::min(static_cast<size_t>(text.find(ibT(';'), literalsFrom)), length);
	if (literalsFrom < literalsEnd) {
		ibString literals = text.Mid(literalsFrom, literalsEnd - literalsFrom);
		literals.Trim(true); literals.Trim(false);
		mask.m_keepLiterals = literals != ibT("0");
	}

	// THE BLANK — the character after the second `;`; a space when there is none.
	if (literalsEnd + 1 < length)
		mask.m_blank = text[literalsEnd + 1];

	return mask;
}

ibString ibInputMask::Apply(const ibString& text) const
{
	if (m_number)
		return m_number->Apply(text);
	if (IsEmpty() || Fits(text))
		return text;

	ibString masked;
	for (const ibSlot& slot : m_slots)
		masked.Append(slot.m_kind == ibSlotKind::Literal ? slot.m_literal : m_blank);

	size_t caret = 0;
	for (size_t i = 0; i < text.length(); ++i) {
		if (text[i] != m_blank)
			Type(masked, caret, text[i]);   // a character no position takes is dropped
	}
	return masked;
}

bool ibInputMask::Replace(ibString& masked, size_t from, size_t to, const ibString& text, size_t& caret) const
{
	if (IsEmpty())
		return false;

	if (m_number) {
		const ibString shaped = m_number->Apply(masked);
		ibString edited = shaped;
		from = std::min(from, edited.length());
		to = std::min(std::max(to, from), edited.length());
		const size_t caretBefore = std::min(caret, edited.length());

		// The range goes as text does — unless what is left is no number of the shape (the point struck out between
		// two parts too long together).
		if (from < to) {
			edited.erase(from, to - from);
			if (m_number->Apply(edited) != edited)
				return false;
		}
		size_t at = from;
		for (size_t i = 0; i < text.length(); ++i) {
			if (text[i] != m_blank)
				m_number->Type(edited, at, text[i]);   // a character the number does not take is dropped
		}
		if (edited == shaped && at == caretBefore)
			return false;
		masked = edited;
		caret = at;
		return true;
	}

	if (!Fits(masked))
		masked = Apply(masked);

	const size_t count = m_slots.size();
	from = std::min(from, count);
	to = std::min(std::max(to, from), count);
	caret = std::min(caret, count);

	const ibString before = masked;
	const size_t caretBefore = caret;

	if (from < to) {
		// A deletion of literals alone reaches the nearest position on the side the caret leaves.
		bool hasPosition = false;
		for (size_t i = from; i < to && !hasPosition; ++i)
			hasPosition = !IsLiteral(i);
		if (!hasPosition && text.IsEmpty()) {
			if (caret >= to) {
				size_t p = from;
				while (p > 0 && IsLiteral(p - 1))
					--p;
				if (p == 0)
					return false;
				from = p - 1;
			}
			else {
				size_t p = to;
				while (p < count && IsLiteral(p))
					++p;
				if (p == count)
					return false;
				from = p;
			}
			to = from + 1;
		}
		Blank(masked, from, to);
	}

	// What is typed goes on from where the range began.
	size_t at = from;
	for (size_t i = 0; i < text.length(); ++i) {
		if (text[i] != m_blank)
			Type(masked, at, text[i]);
	}
	caret = at;

	// A literal typed changes no character, only where the caret stands — and that is the key taken too.
	return masked != before || caret != caretBefore;
}

bool ibInputMask::Strip(const ibString& masked, ibString& text, ibString* refusal) const
{
	text.clear();
	if (IsEmpty()) {
		text = masked;
		return true;
	}

	if (m_number) {
		const ibString shaped = m_number->Apply(masked);
		if (std::none_of(shaped.begin(), shaped.end(), IsDecimalDigit))
			return true;   // nothing typed: the empty value
		text = shaped;
		if (!text.IsEmpty() && text.Last() == ibT('.'))
			text.RemoveLast();
		const size_t sign = !text.IsEmpty() && text[0] == ibT('-') ? 1 : 0;
		if (sign < text.length() && text[sign] == ibT('.'))
			text.insert(sign, 1, ibT('0'));
		return true;
	}

	const ibString shaped = Fits(masked) ? masked : Apply(masked);

	// The last position typed into. None: the field was cleared — the empty value, not a value typed in part.
	size_t end = 0;
	for (size_t i = 0; i < m_slots.size(); ++i) {
		if (!IsLiteral(i) && shaped[i] != m_blank)
			end = i + 1;
	}
	if (end == 0)
		return true;

	for (size_t i = 0; i < m_slots.size(); ++i) {
		const ibSlot& slot = m_slots[i];
		if (slot.m_kind == ibSlotKind::Literal) {
			// None after the last character typed: a time left out leaves no `::` behind it.
			if (m_keepLiterals && i < end)
				text.Append(slot.m_literal);
		}
		else if (shaped[i] != m_blank)
			text.Append(shaped[i]);
		else if (slot.m_required) {
			if (refusal != nullptr)
				*refusal = ibString::Format(_("The value is typed in only in part: %s"), shaped);
			text.clear();
			return false;
		}
	}
	return true;
}

bool ibInputMask::GetRun(const ibString& masked, size_t position, size_t& first, size_t& last) const
{
	if (m_number) {
		const ibString shaped = m_number->Apply(masked);
		const size_t sign = !shaped.IsEmpty() && shaped[0] == ibT('-') ? 1 : 0;
		const size_t point = shaped.find(ibT('.'));
		if (point == ibString::npos || position <= point) {
			first = sign;
			last = point == ibString::npos ? shaped.length() : point;   // one past, for now
		}
		else {
			first = point + 1;
			last = shaped.length();
		}
		if (first >= last)
			return false;   // an empty part
		--last;
		return true;
	}

	const size_t count = m_slots.size();
	size_t slot = position;
	if (slot >= count || IsLiteral(slot)) {
		if (slot == 0 || slot > count || IsLiteral(slot - 1))
			return false;
		--slot;
	}
	GetRun(slot, first, last);
	return true;
}

bool ibInputMask::Accepts(size_t slot, ibChar c) const
{
	if (c == m_blank)
		return false;   // a blank typed would be a position left empty
	switch (m_slots[slot].m_kind) {
	case ibSlotKind::Digit:         return IsDecimalDigit(c);
	case ibSlotKind::DigitOrSign:   return IsDecimalDigit(c) || c == ibT('-') || c == ibT('+');
	case ibSlotKind::Letter:        return ibIsAlpha(c);
	case ibSlotKind::LetterOrDigit: return ibIsAlnum(c);
	case ibSlotKind::Any:           return c >= ibT(' ');
	default:                        return false;
	}
}

ibChar ibInputMask::Cased(size_t slot, ibChar c) const
{
	switch (m_slots[slot].m_case) {
	case ibSlotCase::Upper: return ibToUpper(c);
	case ibSlotCase::Lower: return ibToLower(c);
	default:                return c;
	}
}

void ibInputMask::GetRun(size_t slot, size_t& first, size_t& last) const
{
	first = last = slot;
	while (first > 0 && !IsLiteral(first - 1))
		--first;
	while (last + 1 < m_slots.size() && !IsLiteral(last + 1))
		++last;
}

bool ibInputMask::Fits(const ibString& text) const
{
	if (text.length() != m_slots.size())
		return false;
	for (size_t i = 0; i < m_slots.size(); ++i) {
		if (IsLiteral(i) ? text[i] != m_slots[i].m_literal : (text[i] != m_blank && !Accepts(i, text[i])))
			return false;
	}
	return true;
}

bool ibInputMask::Type(ibString& masked, size_t& caret, ibChar c) const
{
	const size_t count = m_slots.size();
	size_t p = std::min(caret, count);

	// Literals before the next position: the one typed is stepped past, any other stepped over.
	while (p < count && IsLiteral(p)) {
		if (StepsPast(m_slots[p].m_literal, c)) {
			caret = p + 1;
			return true;
		}
		++p;
	}
	if (p == count)
		return false;

	if (Accepts(p, c)) {
		const ibChar was = masked[p];
		masked[p] = Cased(p, c);
		if (TakesPart(masked, p)) {
			caret = p + 1;
			return true;
		}
		masked[p] = was;
	}

	// Not taken here: a literal further on that IS this character is stepped past — `.` typed in a day goes on to the
	// month.
	for (size_t q = p + 1; q < count; ++q) {
		if (IsLiteral(q) && StepsPast(m_slots[q].m_literal, c)) {
			caret = q + 1;
			return true;
		}
	}
	return false;
}

bool ibInputMask::TakesPart(ibString& masked, size_t slot) const
{
	const ibDatePart part = m_slots[slot].m_part;
	if (part == ibDatePart::None)
		return true;

	size_t first = 0, last = 0;
	GetRun(slot, first, last);
	long long least = 0, most = 0;
	switch (part) {
	case ibDatePart::Day:    least = 1; most = 31; break;
	case ibDatePart::Month:  least = 1; most = 12; break;
	case ibDatePart::Hour:   least = 0; most = 23; break;
	case ibDatePart::Hour12: least = 1; most = 12; break;
	case ibDatePart::Minute:
	case ibDatePart::Second: least = 0; most = 59; break;
	default: {
		// A year: any of its width, the year 0 none.
		most = 1;
		for (size_t i = first; i <= last; ++i)
			most *= 10;
		least = last - first + 1 >= 4 ? 1 : 0;
		--most;
		break;
	}
	}

	// The digits from the part's start to the slot — a beginning no value of the part has is refused. One left blank
	// before it says nothing yet.
	long long prefix = 0;
	bool gap = false;
	for (size_t i = first; i <= slot && !gap; ++i) {
		gap = masked[i] == m_blank;
		if (!gap)
			prefix = prefix * 10 + (masked[i] - ibT('0'));
	}
	if (!gap) {
		long long scale = 1;
		for (size_t i = slot; i < last; ++i)
			scale *= 10;
		if (prefix * scale > most || prefix * scale + scale - 1 < least)
			return false;
	}

	// What stands after it, where it makes the part impossible, goes: `15` typed over with `3` is `3_`, then `30`.
	long long value = 0;
	bool whole = true;
	for (size_t i = first; i <= last && whole; ++i) {
		whole = masked[i] != m_blank;
		if (whole)
			value = value * 10 + (masked[i] - ibT('0'));
	}
	if (whole && (value < least || value > most)) {
		for (size_t i = slot + 1; i <= last; ++i)
			masked[i] = m_blank;
	}
	return true;
}

void ibInputMask::Blank(ibString& masked, size_t from, size_t to) const
{
	for (size_t i = from; i < to; ++i) {
		if (!IsLiteral(i))
			masked[i] = m_blank;
	}
}

ibString ibInputMask::NumberMask(int whole, int fraction, bool withSign)
{
	ibString text = ibT("!");
	if (withSign)
		text.Append(ibT('#'));
	text.Append(ibT('9'), static_cast<size_t>(std::max(whole, 1)));   // a number all fraction still writes its 0
	if (fraction > 0) {
		text.Append(ibT('.'));
		text.Append(ibT('9'), static_cast<size_t>(fraction));
	}
	return text;
}

bool ibInputMask::StepsPast(ibChar literal, ibChar c)
{
	return c == literal || (IsPoint(literal) && IsPoint(c));
}

// ------------------------------- a number -----------------------------------

ibString ibInputMask::ibNumberShape::Apply(const ibString& text) const
{
	ibString number;
	bool negative = false, point = false, digits = false;
	ibString wholePart, fractionPart;
	for (size_t i = 0; i < text.length(); ++i) {
		const ibChar c = text[i];
		if (IsDecimalDigit(c)) {
			digits = true;
			if (!point && wholePart.length() < m_whole)
				wholePart.Append(c);
			else if (point && fractionPart.length() < m_fraction)
				fractionPart.Append(c);
		}
		else if (c == ibT('-') && m_signed && !digits && !point)
			negative = true;
		else if (IsPoint(c) && m_fraction > 0 && !point)
			point = true;
		// anything else — a group's space, a blank, a letter — is no part of a number
	}
	if (negative)
		number.Append(ibT('-'));
	number += wholePart;
	if (point) {
		number.Append(ibT('.'));
		number += fractionPart;
	}
	return number;
}

bool ibInputMask::ibNumberShape::Type(ibString& text, size_t& caret, ibChar c) const
{
	const size_t sign = !text.IsEmpty() && text[0] == ibT('-') ? 1 : 0;
	const size_t point = text.find(ibT('.'));
	caret = std::min(caret, text.length());

	// The sign: `-` puts it before the number or takes it away, `+` takes it away.
	if (c == ibT('-') || c == ibT('+')) {
		if (!m_signed)
			return false;
		if (sign != 0) {
			text.erase(0, 1);
			caret = caret > 0 ? caret - 1 : 0;
			return true;
		}
		if (c == ibT('+'))
			return false;
		text.insert(0, 1, ibT('-'));
		++caret;
		return true;
	}

	caret = std::max(caret, sign);   // nothing goes before the sign

	// The point: where it already is, the caret goes past it; where it is not, it goes in at the caret — what stands
	// after it becomes the fraction, when the fraction can hold it.
	if (IsPoint(c)) {
		if (m_fraction == 0)
			return false;
		if (point != ibString::npos) {
			caret = point + 1;
			return true;
		}
		if (text.length() - caret > m_fraction)
			return false;
		text.insert(caret, 1, ibT('.'));
		++caret;
		return true;
	}

	if (!IsDecimalDigit(c))
		return false;

	if (point == ibString::npos || caret <= point) {
		const size_t wholeEnd = point == ibString::npos ? text.length() : point;
		// A lone zero gives way to the first digit typed after it: `0.000` and `5` is `5.000`, not `05.000`.
		if (wholeEnd - sign == 1 && text[sign] == ibT('0') && caret == sign + 1) {
			text[sign] = c;
			return true;
		}
		if (wholeEnd - sign >= m_whole)
			return false;   // the whole part is full: no more digits than the type keeps
		text.insert(caret, 1, c);
		++caret;
		return true;
	}

	// The fraction: a digit goes in while there is room, and over the next one when there is none.
	if (text.length() - point - 1 < m_fraction) {
		text.insert(caret, 1, c);
		++caret;
		return true;
	}
	if (caret < text.length()) {
		text[caret] = c;
		++caret;
		return true;
	}
	return false;
}
