////////////////////////////////////////////////////////////////////////////
//	Description : The format string as a value (formatString.h)
////////////////////////////////////////////////////////////////////////////

#include "core/formatString.h"
#include "core/variant.h"   // ibVariant — what Apply asks of a value
#include "core/localization/dictionary.h"   // ibDictionary — the language L names, the long date as it writes it
#include <cwchar>      // std::wcstol — a code's number read

namespace {

struct ibFormatCodeInfo {
	ibFormatCode  m_code;
	const ibChar* m_name;
	ibFormatKind  m_kind;
};

// THE SPELLING, in one place: Parse reads by it, Render writes by it, the constructor labels by it.
const ibFormatCodeInfo s_codes[] = {
	{ ibFormatCode::NumberDigits,           ibT("ND"),  ibFormatKind::Number  },
	{ ibFormatCode::NumberFractionDigits,   ibT("NFD"), ibFormatKind::Number  },
	{ ibFormatCode::NumberDecimalSeparator, ibT("NDS"), ibFormatKind::Number  },
	{ ibFormatCode::NumberGroupSeparator,   ibT("NGS"), ibFormatKind::Number  },
	{ ibFormatCode::NumberGroupSize,        ibT("NG"),  ibFormatKind::Number  },
	{ ibFormatCode::NumberZero,             ibT("NZ"),  ibFormatKind::Number  },
	{ ibFormatCode::DatePattern,            ibT("DF"),  ibFormatKind::Date    },
	{ ibFormatCode::DateEmpty,              ibT("DE"),  ibFormatKind::Date    },
	{ ibFormatCode::BooleanTrue,            ibT("BT"),  ibFormatKind::Boolean },
	{ ibFormatCode::BooleanFalse,           ibT("BF"),  ibFormatKind::Boolean },
	{ ibFormatCode::NumberLeadingZero,      ibT("NLZ"), ibFormatKind::Number  },
	{ ibFormatCode::DateLocalFormat,        ibT("DLF"), ibFormatKind::Date    },
	{ ibFormatCode::Language,               ibT("L"),   ibFormatKind::Date    },
};

struct ibDatePresetInfo {
	ibDatePreset  m_preset;
	const ibChar* m_pattern;
};

const ibDatePresetInfo s_presets[] = {
	{ ibDatePreset::Date,         ibT("dd.MM.yyyy")          },
	{ ibDatePreset::DateTime,     ibT("dd.MM.yyyy HH:mm:ss") },
	{ ibDatePreset::Time,         ibT("HH:mm:ss")            },
	{ ibDatePreset::HoursMinutes, ibT("HH:mm")               },
	{ ibDatePreset::MonthYear,    ibT("MM.yyyy")             },
	{ ibDatePreset::Year,         ibT("yyyy")                },
	{ ibDatePreset::Sortable,     ibT("yyyy-MM-dd")          },
};

std::optional<ibChar> FirstChar(const ibString& value)
{
	if (value.IsEmpty())
		return std::nullopt;   // no character named: the number's own separator stands
	return value[0];
}

// The number the text starts with, 0 when it starts with none — what wxAtoi read.
int FirstNumber(const ibString& value)
{
	return static_cast<int>(std::wcstol(value.wc_str(), nullptr, 10));
}

// The letters of a date pattern. ⚠ `MM` IS THE MONTH and `mm` the minutes, as everywhere else they are written.
enum ibDateLetter { eYear, eMonth, eDay, eHour24, eHour12, eMinute, eSecond, eAmPm, eNone };
ibDateLetter DateLetter(ibChar c)
{
	switch (c) {
	case L'y': return eYear;
	case L'M': return eMonth;
	case L'd': return eDay;
	case L'H': return eHour24;
	case L'h': return eHour12;
	case L'm': return eMinute;
	case L's': return eSecond;
	case L't': return eAmPm;
	}
	return eNone;
}

void AppendTwoDigits(int value, ibString& out)
{
	out += static_cast<ibChar>(L'0' + value / 10 % 10);
	out += static_cast<ibChar>(L'0' + value % 10);
}

// A field of a date: below 100 always (a year comes as its last two digits), one digit when one is asked and
// one is enough.
void AppendNumber(int value, size_t width, ibString& out)
{
	if (width >= 2 || value >= 10) AppendTwoDigits(value, out);
	else out += static_cast<ibChar>(L'0' + value);
}

// ⭐ THE DATE PATTERN WRITTEN STRAIGHT INTO `out`: a run of one letter is one field, its length picking the
// form - `d` 1, `dd` 01, `ddd` Mon, `dddd` Monday; `M` 1, `MM` 01, `MMM` Oct, `MMMM` October; `y` 6, `yy` 26,
// `yyyy` 2026; `H`/`HH` hours, `h`/`hh` on a 12-hour clock, `m`/`mm` minutes, `s`/`ss` seconds, `tt` AM/PM.
// Text in single quotes stands as written; every other character stands as it is. The names are the date's
// (ibDateTime::MonthName and the rest), in the language of `dictionary` - L's - or the thread's; `MMMM` after
// a day is the month as it stands there, the genitive in a language with cases. No strftime pattern is built:
// a report prints a date per row.
void WriteDate(const ibString& pattern, const ibDateTimeParts& tm, const ibDictionary* dictionary, ibString& out)
{
	const size_t length = pattern.length();
	bool hasDay = false;
	for (size_t i = 0; i < length; i++)
		if (DateLetter(pattern[i]) == eDay) { hasDay = true; break; }

	out.clear();
	out.reserve(length + 8);
	for (size_t i = 0; i < length;) {
		const ibChar c = pattern[i];
		if (c == L'\'') {
			size_t close = i + 1;
			while (close < length && pattern[close] != L'\'') ++close;
			out += pattern.Mid(i + 1, close - i - 1);
			i = close < length ? close + 1 : close;
			continue;
		}
		size_t run = 1;
		while (i + run < length && pattern[i + run] == c) ++run;
		switch (DateLetter(c)) {
		case eYear:
			if (run >= 3) {
				if (tm.m_year >= 0 && tm.m_year <= 9999) { AppendTwoDigits(tm.m_year / 100, out); AppendTwoDigits(tm.m_year % 100, out); }
				else out += ibString::Format(ibT("%d"), tm.m_year);
			}
			else AppendNumber((tm.m_year % 100 + 100) % 100, run, out);
			break;
		case eMonth:
			if (run >= 4)      out += hasDay ? ibDateTime::MonthGenitiveName(tm.m_month, dictionary) : ibDateTime::MonthName(tm.m_month, dictionary);
			else if (run == 3) out += ibDateTime::MonthShortName(tm.m_month, dictionary);
			else               AppendNumber(static_cast<int>(tm.m_month), run, out);
			break;
		case eDay:
			if (run >= 4)      out += ibDateTime::WeekDayName(tm.m_weekDay, dictionary);
			else if (run == 3) out += ibDateTime::WeekDayShortName(tm.m_weekDay, dictionary);
			else               AppendNumber(static_cast<int>(tm.m_day), run, out);
			break;
		case eHour24: AppendNumber(static_cast<int>(tm.m_hour), run, out); break;
		case eHour12: AppendNumber(static_cast<int>(tm.m_hour % 12 == 0 ? 12 : tm.m_hour % 12), run, out); break;
		case eMinute: AppendNumber(static_cast<int>(tm.m_minute), run, out); break;
		case eSecond: AppendNumber(static_cast<int>(tm.m_second), run, out); break;
		case eAmPm:   out += tm.m_hour < 12 ? ibT("AM") : ibT("PM"); break;
		case eNone:   out.append(run, c); break;
		}
		i += run;
	}
}

// The long date, `13 October 2026` - marked for the catalog here, said in the language asked: a language's
// catalog gives the order and what follows the year, quoted where it is text ("d MMMM yyyy 'г.'").
const char* const s_longDatePattern = ibTRANSLATE_IN_CONTEXT("date pattern", "d MMMM yyyy");

// DLF: the presets named by letter - D, DD, T, DT, DDT. Empty when the value is none of them.
ibString LocalFormatPattern(const ibString& preset, const ibDictionary* dictionary)
{
	ibString key = preset.Upper();
	key.Trim(true).Trim(false);
	const ibString longDate = ibGetTranslation(dictionary, ibString(s_longDatePattern), ibT("date pattern"));
	if (key == ibT("D"))   return ibT("dd.MM.yyyy");
	if (key == ibT("DD"))  return longDate;
	if (key == ibT("T"))   return ibT("H:mm:ss");
	if (key == ibT("DT"))  return ibT("dd.MM.yyyy H:mm:ss");
	if (key == ibT("DDT")) return longDate + ibT(" H:mm:ss");
	return ibString();
}

void PadWholePart(ibString& text, int width)
{
	size_t start = (!text.IsEmpty() && text[0] == L'-') ? 1 : 0;
	size_t end = start;
	while (end < text.length() && text[end] >= L'0' && text[end] <= L'9') ++end;
	const int digits = static_cast<int>(end - start);
	if (digits < width)
		text.insert(start, static_cast<size_t>(width - digits), L'0');
}

} // namespace

bool ibNumberFormat::operator == (const ibNumberFormat& other) const
{
	return m_digits == other.m_digits && m_fractionDigits == other.m_fractionDigits
		&& m_decimalSeparator == other.m_decimalSeparator && m_groupSeparator == other.m_groupSeparator
		&& m_groupSize == other.m_groupSize && m_zero == other.m_zero && m_leadingZero == other.m_leadingZero;
}

bool ibDateFormat::operator == (const ibDateFormat& other) const
{
	return m_pattern == other.m_pattern && m_empty == other.m_empty && m_localFormat == other.m_localFormat
		&& m_language == other.m_language;
}

bool ibBooleanFormat::operator == (const ibBooleanFormat& other) const
{
	return m_true == other.m_true && m_false == other.m_false;
}

bool ibFormatString::operator == (const ibFormatString& other) const
{
	return m_number == other.m_number && m_date == other.m_date && m_boolean == other.m_boolean
		&& m_other == other.m_other;
}

const std::vector<ibFormatCode>& ibFormatString::Codes()
{
	static const std::vector<ibFormatCode> s_order = [] {
		std::vector<ibFormatCode> order;
		for (const ibFormatCodeInfo& info : s_codes)
			order.push_back(info.m_code);
		return order;
	}();
	return s_order;
}

bool ibFormatString::FindCode(const ibString& name, ibFormatCode& code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (name == info.m_name) { code = info.m_code; return true; }
	}
	return false;
}

std::optional<ibString> ibFormatString::Get(ibFormatCode code) const
{
	const auto number = [](const std::optional<int>& value) {
		return value ? std::optional<ibString>(ibString::Format(ibT("%d"), *value)) : std::nullopt;
	};
	const auto character = [](const std::optional<ibChar>& value) {
		return value ? std::optional<ibString>(ibString(*value)) : std::nullopt;
	};

	switch (code) {
	case ibFormatCode::NumberDigits:           return number(m_number.m_digits);
	case ibFormatCode::NumberFractionDigits:   return number(m_number.m_fractionDigits);
	case ibFormatCode::NumberDecimalSeparator: return character(m_number.m_decimalSeparator);
	case ibFormatCode::NumberGroupSeparator:   return character(m_number.m_groupSeparator);
	case ibFormatCode::NumberGroupSize:        return number(m_number.m_groupSize);
	case ibFormatCode::NumberZero:             return m_number.m_zero;
	case ibFormatCode::DatePattern:            return m_date.m_pattern;
	case ibFormatCode::DateEmpty:              return m_date.m_empty;
	case ibFormatCode::BooleanTrue:            return m_boolean.m_true;
	case ibFormatCode::BooleanFalse:           return m_boolean.m_false;
	case ibFormatCode::NumberLeadingZero:      return m_number.m_leadingZero;
	case ibFormatCode::DateLocalFormat:        return m_date.m_localFormat;
	case ibFormatCode::Language:               return m_date.m_language;
	}
	return std::nullopt;
}

void ibFormatString::Set(ibFormatCode code, const ibString& value)
{
	switch (code) {
	case ibFormatCode::NumberDigits:           m_number.m_digits = FirstNumber(value); break;
	case ibFormatCode::NumberFractionDigits:   m_number.m_fractionDigits = FirstNumber(value); break;
	case ibFormatCode::NumberDecimalSeparator: m_number.m_decimalSeparator = FirstChar(value); break;
	case ibFormatCode::NumberGroupSeparator:   m_number.m_groupSeparator = FirstChar(value); break;
	case ibFormatCode::NumberGroupSize:        m_number.m_groupSize = FirstNumber(value); break;
	case ibFormatCode::NumberZero:             m_number.m_zero = value; break;
	case ibFormatCode::DatePattern:            m_date.m_pattern = value; break;
	case ibFormatCode::DateEmpty:              m_date.m_empty = value; break;
	case ibFormatCode::BooleanTrue:            m_boolean.m_true = value; break;
	case ibFormatCode::BooleanFalse:           m_boolean.m_false = value; break;
	case ibFormatCode::NumberLeadingZero:      m_number.m_leadingZero = value; break;
	case ibFormatCode::DateLocalFormat:        m_date.m_localFormat = value; break;
	case ibFormatCode::Language:               m_date.m_language = value; break;
	}
}

void ibFormatString::Clear(ibFormatCode code)
{
	switch (code) {
	case ibFormatCode::NumberDigits:           m_number.m_digits.reset(); break;
	case ibFormatCode::NumberFractionDigits:   m_number.m_fractionDigits.reset(); break;
	case ibFormatCode::NumberDecimalSeparator: m_number.m_decimalSeparator.reset(); break;
	case ibFormatCode::NumberGroupSeparator:   m_number.m_groupSeparator.reset(); break;
	case ibFormatCode::NumberGroupSize:        m_number.m_groupSize.reset(); break;
	case ibFormatCode::NumberZero:             m_number.m_zero.reset(); break;
	case ibFormatCode::DatePattern:            m_date.m_pattern.reset(); break;
	case ibFormatCode::DateEmpty:              m_date.m_empty.reset(); break;
	case ibFormatCode::BooleanTrue:            m_boolean.m_true.reset(); break;
	case ibFormatCode::BooleanFalse:           m_boolean.m_false.reset(); break;
	case ibFormatCode::NumberLeadingZero:      m_number.m_leadingZero.reset(); break;
	case ibFormatCode::DateLocalFormat:        m_date.m_localFormat.reset(); break;
	case ibFormatCode::Language:               m_date.m_language.reset(); break;
	}
}

const ibChar* ibFormatString::CodeName(ibFormatCode code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (info.m_code == code) return info.m_name;
	}
	return ibT("");
}

ibFormatKind ibFormatString::KindOf(ibFormatCode code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (info.m_code == code) return info.m_kind;
	}
	return ibFormatKind::Number;
}

ibString ibFormatString::PresetPattern(ibDatePreset preset)
{
	for (const ibDatePresetInfo& info : s_presets) {
		if (info.m_preset == preset) return info.m_pattern;
	}
	return ibString();
}

ibDatePreset ibFormatString::PresetOf(const ibString& pattern)
{
	for (const ibDatePresetInfo& info : s_presets) {
		if (pattern == info.m_pattern) return info.m_preset;
	}
	return ibDatePreset::Custom;
}

bool ibFormatString::IsWritable(const ibString& text)
{
	return text.Find(ibT(';')) == wxNOT_FOUND && text.Find(ibT('=')) == wxNOT_FOUND;
}

ibFormatString ibFormatString::Parse(const ibString& fmt)
{
	ibFormatString format;
	Parse(fmt, format);
	return format;
}

bool ibFormatString::Parse(const ibString& fmt, ibFormatString& format)
{
	// The pairs, as Format has always read them: `;` ends one, the first `=` splits it, and a `=`
	// after that is not kept.
	std::vector<std::pair<ibString, ibString>> pairs;
	ibString leftParam, rightParam;
	bool bLeftParam = true;
	// ⚠ A VALUE THAT IS ALL SPACE IS A SPACE, not nothing: `NGS= ` asks for a space between the digit
	// groups - the help's own example - and trimming it to empty left the number ungrouped.
	const auto commit = [&]() {
		leftParam.Trim(true); leftParam.Trim(false);
		const bool allSpace = !rightParam.IsEmpty() && ibString(rightParam).Trim(true).Trim(false).IsEmpty();
		rightParam.Trim(true); rightParam.Trim(false);
		// A later pair of the same code wins, as it did when the pairs went into a map.
		const ibString value = allSpace ? ibString(ibT(" ")) : rightParam;
		bool replaced = false;
		for (auto& pair : pairs) {
			if (pair.first == leftParam) { pair.second = value; replaced = true; break; }
		}
		if (!replaced && !leftParam.IsEmpty())
			pairs.emplace_back(leftParam, value);
		bLeftParam = true; leftParam = ""; rightParam = "";
	};
	for (unsigned int i = 0; i < fmt.length(); i++) {
		auto c = fmt[i];
		if (c == ';') {
			commit();
			continue;
		}
		else if (c == '=') {
			bLeftParam = false;
		}

		if (c != '=') {
			if (bLeftParam) {
				leftParam += c;
			}
			else {
				rightParam += c;
			}
		}

		if (i == fmt.length() - 1)
			commit();
	}

	for (const auto& pair : pairs) {
		ibFormatCode code = ibFormatCode::NumberDigits;
		if (FindCode(pair.first, code))
			format.Set(code, pair.second);
		else
			format.m_other.push_back(pair);
	}
	return !pairs.empty();
}

ibString ibFormatString::Render() const
{
	ibString text;
	const auto put = [&text](const ibString& name, const ibString& value) {
		if (!text.IsEmpty()) text += ibT("; ");
		text += name + ibT("=") + value;
	};

	// In the order of the tabs, each kind's codes in the order of the table; then what is only kept.
	for (const ibFormatCode code : Codes()) {
		if (const std::optional<ibString> value = Get(code))
			put(CodeName(code), *value);
	}
	for (const auto& pair : m_other)
		put(pair.first, pair.second);
	return text;
}

ibString ibFormatString::Apply(const ibVariant& value) const
{
	ibString result;
	Apply(value, result);
	return result;
}

bool ibFormatString::Apply(const ibVariant& value, ibString& result) const
{
	switch (value.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: {
		const std::optional<ibString>& said = value.GetBoolean() ? m_boolean.m_true : m_boolean.m_false;
		if (said)
			result = *said;
		else
			result = value.GetString();
		return !result.IsEmpty();
	}
	case ibValueTypes::TYPE_NUMBER: {
		const ibNumber number = value.GetNumber();

		// NZ: replacement string when value is exactly zero.
		if (number.IsZero() && m_number.m_zero) {
			result = *m_number.m_zero;
			return !result.IsEmpty();
		}

		ibNumber::Format numFmt;
		if (m_number.m_fractionDigits)   numFmt.fracDigits = *m_number.m_fractionDigits;
		if (m_number.m_digits)           numFmt.precision  = *m_number.m_digits;
		if (m_number.m_decimalSeparator) numFmt.decimalSep = *m_number.m_decimalSeparator;
		if (m_number.m_groupSeparator) {
			numFmt.groupSep  = *m_number.m_groupSeparator;
			numFmt.groupSize = 3;   // a separator names no group size of its own: thousands, unless NG says
		}
		if (m_number.m_groupSize)        numFmt.groupSize  = *m_number.m_groupSize;

		number.ToString(numFmt, result);
		// NLZ: the whole part padded with zeros to the digits ND leaves it (ND - NFD).
		if (m_number.m_leadingZero && m_number.m_digits)
			PadWholePart(result, *m_number.m_digits - m_number.m_fractionDigits.value_or(0));
		return !result.IsEmpty();
	}
	case ibValueTypes::TYPE_DATE:
		if (value.IsEmpty() && m_date.m_empty)
			result = *m_date.m_empty;
		else {
			// The language L names, its own dictionary; with none (or one this engine has no name for) the thread's.
			const ibDictionary* const dictionary = m_date.m_language ? ibDictionary::ForLanguage(*m_date.m_language) : nullptr;
			// DF first; DLF when there is no DF - a pattern written out wins over a preset named.
			ibString pattern;
			if (m_date.m_pattern)
				pattern = *m_date.m_pattern;
			else if (m_date.m_localFormat)
				pattern = LocalFormatPattern(*m_date.m_localFormat, dictionary);
			if (pattern.IsEmpty()) {
				result = value.GetString();
				return !result.IsEmpty();
			}
			ibDateTimeParts parts;
			value.GetDate().ToParts(parts);
			WriteDate(pattern, parts, dictionary, result);
		}
		return !result.IsEmpty();
	default:
		result = value.GetString();   // every other type formats as its plain string
		return !result.IsEmpty();
	}
}
