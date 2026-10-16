////////////////////////////////////////////////////////////////////////////
//	Description : The format string as a value (formatString.h)
////////////////////////////////////////////////////////////////////////////

#include "core/formatString.h"
#include "core/anyValue.h"   // ibAnyValue — what Apply asks of a value

#include <wx/datetime.h>
#include <wx/wxcrt.h>   // wxAtoi — a code's number read

#include <algorithm>   // std::max / std::min — the spelling of a date letter

namespace {

struct ibFormatCodeInfo {
	ibFormatCode  m_code;
	const wxChar* m_name;
	ibFormatKind  m_kind;
};

// THE SPELLING, in one place: Parse reads by it, Render writes by it, the constructor labels by it.
const ibFormatCodeInfo s_codes[] = {
	{ ibFormatCode::NumberDigits,           wxT("ND"),  ibFormatKind::Number  },
	{ ibFormatCode::NumberFractionDigits,   wxT("NFD"), ibFormatKind::Number  },
	{ ibFormatCode::NumberDecimalSeparator, wxT("NDS"), ibFormatKind::Number  },
	{ ibFormatCode::NumberGroupSeparator,   wxT("NGS"), ibFormatKind::Number  },
	{ ibFormatCode::NumberGroupSize,        wxT("NG"),  ibFormatKind::Number  },
	{ ibFormatCode::NumberZero,             wxT("NZ"),  ibFormatKind::Number  },
	{ ibFormatCode::DatePattern,            wxT("DF"),  ibFormatKind::Date    },
	{ ibFormatCode::DateEmpty,              wxT("DE"),  ibFormatKind::Date    },
	{ ibFormatCode::BooleanTrue,            wxT("BT"),  ibFormatKind::Boolean },
	{ ibFormatCode::BooleanFalse,           wxT("BF"),  ibFormatKind::Boolean },
	{ ibFormatCode::NumberLeadingZero,      wxT("NLZ"), ibFormatKind::Number  },
	{ ibFormatCode::DateLocalFormat,        wxT("DLF"), ibFormatKind::Date    },
	{ ibFormatCode::Language,               wxT("L"),   ibFormatKind::Date    },
};

// THE RUSSIAN SPELLINGS 1C reads as well (issue #429): read, never written - Render writes the English name.
struct ibFormatAlias {
	const wxChar* m_name;
	ibFormatCode  m_code;
};

const ibFormatAlias s_aliases[] = {
	{ wxT("\u0427\u0426"), ibFormatCode::NumberDigits },   // ND
	{ wxT("\u0427\u0414\u0426"), ibFormatCode::NumberFractionDigits },   // NFD
	{ wxT("\u0427\u0420\u0414"), ibFormatCode::NumberDecimalSeparator },   // NDS
	{ wxT("\u0427\u0420\u0413"), ibFormatCode::NumberGroupSeparator },   // NGS
	{ wxT("\u0427\u0413"), ibFormatCode::NumberGroupSize },   // NG
	{ wxT("\u0427\u041D"), ibFormatCode::NumberZero },   // NZ
	{ wxT("\u0427\u0412\u041D"), ibFormatCode::NumberLeadingZero },   // NLZ
	{ wxT("\u0414\u0424"), ibFormatCode::DatePattern },   // DF
	{ wxT("\u0414\u041F"), ibFormatCode::DateEmpty },   // DE
	{ wxT("\u0414\u041B\u0424"), ibFormatCode::DateLocalFormat },   // DLF
	{ wxT("\u0411\u0418"), ibFormatCode::BooleanTrue },   // BT
	{ wxT("\u0411\u041B"), ibFormatCode::BooleanFalse },   // BF
	{ wxT("\u041B"), ibFormatCode::Language },   // L
};

struct ibDatePresetInfo {
	ibDatePreset  m_preset;
	const wxChar* m_pattern;
};

const ibDatePresetInfo s_presets[] = {
	{ ibDatePreset::Date,         wxT("dd.MM.yyyy")          },
	{ ibDatePreset::DateTime,     wxT("dd.MM.yyyy HH:mm:ss") },
	{ ibDatePreset::Time,         wxT("HH:mm:ss")            },
	{ ibDatePreset::HoursMinutes, wxT("HH:mm")               },
	{ ibDatePreset::MonthYear,    wxT("MM.yyyy")             },
	{ ibDatePreset::Year,         wxT("yyyy")                },
	{ ibDatePreset::Sortable,     wxT("yyyy-MM-dd")          },
};

std::optional<wxUniChar> FirstChar(const wxString& value)
{
	if (value.IsEmpty())
		return std::nullopt;   // no character named: the number's own separator stands
	return value[0];
}

// The names a pattern spells out (MMMM, MMM, dddd, ddd), per language. Non-ASCII text is escaped so the
// source stays ASCII. MMMM after a day is the genitive (13 October, not 13 October-nominative in ru/uk), on its own the nominative.
struct ibDateNames {
	const wxChar* const* m_month;        // nominative
	const wxChar* const* m_monthOf;      // genitive
	const wxChar* const* m_monthShort;
	const wxChar* const* m_day;
	const wxChar* const* m_dayShort;
	const wxChar*        m_yearSuffix;   // after the year of a long date
};
const wxChar* const s_enMonth[] = { wxT("January"), wxT("February"), wxT("March"), wxT("April"), wxT("May"), wxT("June"), wxT("July"), wxT("August"), wxT("September"), wxT("October"), wxT("November"), wxT("December") };
const wxChar* const s_enMonthShort[] = { wxT("Jan"), wxT("Feb"), wxT("Mar"), wxT("Apr"), wxT("May"), wxT("Jun"), wxT("Jul"), wxT("Aug"), wxT("Sep"), wxT("Oct"), wxT("Nov"), wxT("Dec") };
const wxChar* const s_enDay[] = { wxT("Monday"), wxT("Tuesday"), wxT("Wednesday"), wxT("Thursday"), wxT("Friday"), wxT("Saturday"), wxT("Sunday") };
const wxChar* const s_enDayShort[] = { wxT("Mon"), wxT("Tue"), wxT("Wed"), wxT("Thu"), wxT("Fri"), wxT("Sat"), wxT("Sun") };
const wxChar* const s_ruMonth[] = { wxT("\u044F\u043D\u0432\u0430\u0440\u044C"), wxT("\u0444\u0435\u0432\u0440\u0430\u043B\u044C"), wxT("\u043C\u0430\u0440\u0442"), wxT("\u0430\u043F\u0440\u0435\u043B\u044C"), wxT("\u043C\u0430\u0439"), wxT("\u0438\u044E\u043D\u044C"), wxT("\u0438\u044E\u043B\u044C"), wxT("\u0430\u0432\u0433\u0443\u0441\u0442"), wxT("\u0441\u0435\u043D\u0442\u044F\u0431\u0440\u044C"), wxT("\u043E\u043A\u0442\u044F\u0431\u0440\u044C"), wxT("\u043D\u043E\u044F\u0431\u0440\u044C"), wxT("\u0434\u0435\u043A\u0430\u0431\u0440\u044C") };
const wxChar* const s_ruMonthOf[] = { wxT("\u044F\u043D\u0432\u0430\u0440\u044F"), wxT("\u0444\u0435\u0432\u0440\u0430\u043B\u044F"), wxT("\u043C\u0430\u0440\u0442\u0430"), wxT("\u0430\u043F\u0440\u0435\u043B\u044F"), wxT("\u043C\u0430\u044F"), wxT("\u0438\u044E\u043D\u044F"), wxT("\u0438\u044E\u043B\u044F"), wxT("\u0430\u0432\u0433\u0443\u0441\u0442\u0430"), wxT("\u0441\u0435\u043D\u0442\u044F\u0431\u0440\u044F"), wxT("\u043E\u043A\u0442\u044F\u0431\u0440\u044F"), wxT("\u043D\u043E\u044F\u0431\u0440\u044F"), wxT("\u0434\u0435\u043A\u0430\u0431\u0440\u044F") };
const wxChar* const s_ruMonthShort[] = { wxT("\u044F\u043D\u0432"), wxT("\u0444\u0435\u0432"), wxT("\u043C\u0430\u0440"), wxT("\u0430\u043F\u0440"), wxT("\u043C\u0430\u044F"), wxT("\u0438\u044E\u043D"), wxT("\u0438\u044E\u043B"), wxT("\u0430\u0432\u0433"), wxT("\u0441\u0435\u043D"), wxT("\u043E\u043A\u0442"), wxT("\u043D\u043E\u044F"), wxT("\u0434\u0435\u043A") };
const wxChar* const s_ruDay[] = { wxT("\u043F\u043E\u043D\u0435\u0434\u0435\u043B\u044C\u043D\u0438\u043A"), wxT("\u0432\u0442\u043E\u0440\u043D\u0438\u043A"), wxT("\u0441\u0440\u0435\u0434\u0430"), wxT("\u0447\u0435\u0442\u0432\u0435\u0440\u0433"), wxT("\u043F\u044F\u0442\u043D\u0438\u0446\u0430"), wxT("\u0441\u0443\u0431\u0431\u043E\u0442\u0430"), wxT("\u0432\u043E\u0441\u043A\u0440\u0435\u0441\u0435\u043D\u044C\u0435") };
const wxChar* const s_ruDayShort[] = { wxT("\u041F\u043D"), wxT("\u0412\u0442"), wxT("\u0421\u0440"), wxT("\u0427\u0442"), wxT("\u041F\u0442"), wxT("\u0421\u0431"), wxT("\u0412\u0441") };
const wxChar* const s_ukMonth[] = { wxT("\u0441\u0456\u0447\u0435\u043D\u044C"), wxT("\u043B\u044E\u0442\u0438\u0439"), wxT("\u0431\u0435\u0440\u0435\u0437\u0435\u043D\u044C"), wxT("\u043A\u0432\u0456\u0442\u0435\u043D\u044C"), wxT("\u0442\u0440\u0430\u0432\u0435\u043D\u044C"), wxT("\u0447\u0435\u0440\u0432\u0435\u043D\u044C"), wxT("\u043B\u0438\u043F\u0435\u043D\u044C"), wxT("\u0441\u0435\u0440\u043F\u0435\u043D\u044C"), wxT("\u0432\u0435\u0440\u0435\u0441\u0435\u043D\u044C"), wxT("\u0436\u043E\u0432\u0442\u0435\u043D\u044C"), wxT("\u043B\u0438\u0441\u0442\u043E\u043F\u0430\u0434"), wxT("\u0433\u0440\u0443\u0434\u0435\u043D\u044C") };
const wxChar* const s_ukMonthOf[] = { wxT("\u0441\u0456\u0447\u043D\u044F"), wxT("\u043B\u044E\u0442\u043E\u0433\u043E"), wxT("\u0431\u0435\u0440\u0435\u0437\u043D\u044F"), wxT("\u043A\u0432\u0456\u0442\u043D\u044F"), wxT("\u0442\u0440\u0430\u0432\u043D\u044F"), wxT("\u0447\u0435\u0440\u0432\u043D\u044F"), wxT("\u043B\u0438\u043F\u043D\u044F"), wxT("\u0441\u0435\u0440\u043F\u043D\u044F"), wxT("\u0432\u0435\u0440\u0435\u0441\u043D\u044F"), wxT("\u0436\u043E\u0432\u0442\u043D\u044F"), wxT("\u043B\u0438\u0441\u0442\u043E\u043F\u0430\u0434\u0430"), wxT("\u0433\u0440\u0443\u0434\u043D\u044F") };
const wxChar* const s_ukMonthShort[] = { wxT("\u0441\u0456\u0447"), wxT("\u043B\u044E\u0442"), wxT("\u0431\u0435\u0440"), wxT("\u043A\u0432\u0456"), wxT("\u0442\u0440\u0430"), wxT("\u0447\u0435\u0440"), wxT("\u043B\u0438\u043F"), wxT("\u0441\u0435\u0440"), wxT("\u0432\u0435\u0440"), wxT("\u0436\u043E\u0432"), wxT("\u043B\u0438\u0441"), wxT("\u0433\u0440\u0443") };
const wxChar* const s_ukDay[] = { wxT("\u043F\u043E\u043D\u0435\u0434\u0456\u043B\u043E\u043A"), wxT("\u0432\u0456\u0432\u0442\u043E\u0440\u043E\u043A"), wxT("\u0441\u0435\u0440\u0435\u0434\u0430"), wxT("\u0447\u0435\u0442\u0432\u0435\u0440"), wxT("\u043F'\u044F\u0442\u043D\u0438\u0446\u044F"), wxT("\u0441\u0443\u0431\u043E\u0442\u0430"), wxT("\u043D\u0435\u0434\u0456\u043B\u044F") };
const wxChar* const s_ukDayShort[] = { wxT("\u041F\u043D"), wxT("\u0412\u0442"), wxT("\u0421\u0440"), wxT("\u0427\u0442"), wxT("\u041F\u0442"), wxT("\u0421\u0431"), wxT("\u041D\u0434") };
const ibDateNames s_en = { s_enMonth, s_enMonth, s_enMonthShort, s_enDay, s_enDayShort, wxT("") };
const ibDateNames s_ru = { s_ruMonth, s_ruMonthOf, s_ruMonthShort, s_ruDay, s_ruDayShort, wxT(" \u0433.") };
const ibDateNames s_uk = { s_ukMonth, s_ukMonthOf, s_ukMonthShort, s_ukDay, s_ukDayShort, wxT(" \u0440.") };

// `L=ru`, `L=uk_UA`, `L=en`: the language part decides; anything else is English.
const ibDateNames& NamesFor(const std::optional<wxString>& language)
{
	if (!language) return s_en;
	const wxString code = language->Left(2).Lower();
	if (code == wxT("ru")) return s_ru;
	if (code == wxT("uk")) return s_uk;
	return s_en;
}

// The letters of a date pattern as 1C reads them, Latin or Russian (Cyrillic letters escaped).
enum ibDateLetter { eYear, eMonth, eDay, eHour24, eHour12, eMinute, eSecond, eAmPm, eNone };
ibDateLetter DateLetter(wxUniChar c)
{
	switch (c.GetValue()) {
	case L'y': case 0x0433 /* g */: return eYear;
	case L'M': case 0x041C /* M */: return eMonth;
	case L'd': case 0x0434 /* d */: return eDay;
	case L'H': case 0x0427 /* Ch */: return eHour24;
	case L'h': case 0x0447 /* ch */: return eHour12;
	case L'm': case 0x043C /* m */: return eMinute;
	case L's': case 0x0441 /* s */: return eSecond;
	case L't': return eAmPm;
	}
	return eNone;
}

void AppendTwoDigits(int value, wxString& out)
{
	out += static_cast<wxChar>(L'0' + value / 10 % 10);
	out += static_cast<wxChar>(L'0' + value % 10);
}

void AppendNumber(int value, size_t width, wxString& out)
{
	if (width >= 2) AppendTwoDigits(value, out);
	else out << value;
}

// ⭐ THE DATE PATTERN WRITTEN STRAIGHT INTO `out`, as 1C reads it (issue #429): a run of one letter is one
// field, its length picking the form - `d` 1, `dd` 01, `ddd` Mon, `dddd` Monday; `M` 1, `MM` 01, `MMM` Oct,
// `MMMM` October; `y` 6, `yy` 26, `yyyy` 2026; `H`/`HH` hours, `h`/`hh` on a 12-hour clock, `m`/`mm`
// minutes, `s`/`ss` seconds, `tt` AM/PM. Text in single quotes stands as written; every other character
// stands as it is. No strftime pattern is built: a report prints a date per row.
void WriteDate(const wxString& pattern, const ibDateTimeParts& tm, const ibDateNames& names, wxString& out)
{
	const size_t length = pattern.length();
	bool hasDay = false;
	for (size_t i = 0; i < length; i++)
		if (DateLetter(pattern[i]) == eDay) { hasDay = true; break; }

	out.clear();
	out.reserve(length + 8);
	for (size_t i = 0; i < length;) {
		const wxUniChar c = pattern[i];
		if (c == L'\'') {
			size_t close = i + 1;
			while (close < length && pattern[close] != L'\'') ++close;
			out += pattern.Mid(i + 1, close - i - 1);
			i = close < length ? close + 1 : close;
			continue;
		}
		size_t run = 1;
		while (i + run < length && pattern[i + run] == c) ++run;
		const unsigned month = std::min(std::max(tm.m_month, 1u), 12u) - 1;
		const unsigned weekDay = std::min(std::max(tm.m_weekDay, 1u), 7u) - 1;
		switch (DateLetter(c)) {
		case eYear:
			if (run >= 3) {
				if (tm.m_year >= 0 && tm.m_year <= 9999) { AppendTwoDigits(tm.m_year / 100, out); AppendTwoDigits(tm.m_year % 100, out); }
				else out << tm.m_year;
			}
			else AppendNumber((tm.m_year % 100 + 100) % 100, run, out);
			break;
		case eMonth:
			if (run >= 4)      out += hasDay ? names.m_monthOf[month] : names.m_month[month];
			else if (run == 3) out += names.m_monthShort[month];
			else               AppendNumber(static_cast<int>(tm.m_month), run, out);
			break;
		case eDay:
			if (run >= 4)      out += names.m_day[weekDay];
			else if (run == 3) out += names.m_dayShort[weekDay];
			else               AppendNumber(static_cast<int>(tm.m_day), run, out);
			break;
		case eHour24: AppendNumber(static_cast<int>(tm.m_hour), run, out); break;
		case eHour12: AppendNumber(static_cast<int>(tm.m_hour % 12 == 0 ? 12 : tm.m_hour % 12), run, out); break;
		case eMinute: AppendNumber(static_cast<int>(tm.m_minute), run, out); break;
		case eSecond: AppendNumber(static_cast<int>(tm.m_second), run, out); break;
		case eAmPm:   out += tm.m_hour < 12 ? wxT("AM") : wxT("PM"); break;
		case eNone:   out.append(run, c); break;
		}
		i += run;
	}
}

// DLF: the presets 1C names by letter - D, DD, T, DT, DDT, or in Russian D=0x0414, T=0x0412. Empty when the
// value is none of them.
wxString LocalFormatPattern(const wxString& preset, const ibDateNames& names)
{
	wxString key;
	for (const wxUniChar ch : preset) {
		if (ch == 0x0414) key += L'D';        // Cyrillic De
		else if (ch == 0x0412) key += L'T';   // Cyrillic Ve
		else key += wxString(ch).Upper();
	}
	key.Trim(true).Trim(false);
	// The long date: `13 October 2026`, and in ru/uk with the year suffix - the suffix quoted so its letters are text.
	const wxString suffix = names.m_yearSuffix;
	const wxString longDate = wxT("d MMMM yyyy") + (suffix.IsEmpty() ? wxString() : wxT("'") + suffix + wxT("'"));
	if (key == wxT("D"))   return wxT("dd.MM.yyyy");
	if (key == wxT("DD"))  return longDate;
	if (key == wxT("T"))   return wxT("H:mm:ss");
	if (key == wxT("DT"))  return wxT("dd.MM.yyyy H:mm:ss");
	if (key == wxT("DDT")) return longDate + wxT(" H:mm:ss");
	return wxString();
}

void PadWholePart(wxString& text, int width)
{
	size_t start = (!text.IsEmpty() && text[0] == L'-') ? 1 : 0;
	size_t end = start;
	while (end < text.length() && wxIsdigit(text[end])) ++end;
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
	return m_pattern == other.m_pattern && m_empty == other.m_empty && m_localFormat == other.m_localFormat;
}

bool ibBooleanFormat::operator == (const ibBooleanFormat& other) const
{
	return m_true == other.m_true && m_false == other.m_false;
}

bool ibFormatString::operator == (const ibFormatString& other) const
{
	return m_number == other.m_number && m_date == other.m_date && m_boolean == other.m_boolean
		&& m_language == other.m_language && m_other == other.m_other;
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

bool ibFormatString::FindCode(const wxString& name, ibFormatCode& code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (name == info.m_name) { code = info.m_code; return true; }
	}
	for (const ibFormatAlias& alias : s_aliases) {
		if (name == alias.m_name) { code = alias.m_code; return true; }
	}
	return false;
}

std::optional<wxString> ibFormatString::Get(ibFormatCode code) const
{
	const auto number = [](const std::optional<int>& value) {
		return value ? std::optional<wxString>(wxString::Format(wxT("%d"), *value)) : std::nullopt;
	};
	const auto character = [](const std::optional<wxUniChar>& value) {
		return value ? std::optional<wxString>(wxString(*value)) : std::nullopt;
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
	case ibFormatCode::Language:               return m_language;
	}
	return std::nullopt;
}

void ibFormatString::Set(ibFormatCode code, const wxString& value)
{
	switch (code) {
	case ibFormatCode::NumberDigits:           m_number.m_digits = wxAtoi(value); break;
	case ibFormatCode::NumberFractionDigits:   m_number.m_fractionDigits = wxAtoi(value); break;
	case ibFormatCode::NumberDecimalSeparator: m_number.m_decimalSeparator = FirstChar(value); break;
	case ibFormatCode::NumberGroupSeparator:   m_number.m_groupSeparator = FirstChar(value); break;
	case ibFormatCode::NumberGroupSize:        m_number.m_groupSize = wxAtoi(value); break;
	case ibFormatCode::NumberZero:             m_number.m_zero = value; break;
	case ibFormatCode::DatePattern:            m_date.m_pattern = value; break;
	case ibFormatCode::DateEmpty:              m_date.m_empty = value; break;
	case ibFormatCode::BooleanTrue:            m_boolean.m_true = value; break;
	case ibFormatCode::BooleanFalse:           m_boolean.m_false = value; break;
	case ibFormatCode::NumberLeadingZero:      m_number.m_leadingZero = value; break;
	case ibFormatCode::DateLocalFormat:        m_date.m_localFormat = value; break;
	case ibFormatCode::Language:               m_language = value; break;
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
	case ibFormatCode::Language:               m_language.reset(); break;
	}
}

const wxChar* ibFormatString::CodeName(ibFormatCode code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (info.m_code == code) return info.m_name;
	}
	return wxT("");
}

ibFormatKind ibFormatString::KindOf(ibFormatCode code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (info.m_code == code) return info.m_kind;
	}
	return ibFormatKind::Number;
}

wxString ibFormatString::PresetPattern(ibDatePreset preset)
{
	for (const ibDatePresetInfo& info : s_presets) {
		if (info.m_preset == preset) return info.m_pattern;
	}
	return wxString();
}

ibDatePreset ibFormatString::PresetOf(const wxString& pattern)
{
	for (const ibDatePresetInfo& info : s_presets) {
		if (pattern == info.m_pattern) return info.m_preset;
	}
	return ibDatePreset::Custom;
}

bool ibFormatString::IsWritable(const wxString& text)
{
	return text.Find(wxT(';')) == wxNOT_FOUND && text.Find(wxT('=')) == wxNOT_FOUND;
}

ibFormatString ibFormatString::Parse(const wxString& fmt)
{
	ibFormatString format;
	Parse(fmt, format);
	return format;
}

bool ibFormatString::Parse(const wxString& fmt, ibFormatString& format)
{
	// The pairs, as Format has always read them: `;` ends one, the first `=` splits it, and a `=`
	// after that is not kept.
	std::vector<std::pair<wxString, wxString>> pairs;
	wxString leftParam, rightParam;
	bool bLeftParam = true;
	// ⚠ A VALUE THAT IS ALL SPACE IS A SPACE, not nothing: `NGS= ` asks for a space between the digit
	// groups - the help's own example - and trimming it to empty left the number ungrouped.
	const auto commit = [&]() {
		leftParam.Trim(true); leftParam.Trim(false);
		const bool allSpace = !rightParam.IsEmpty() && wxString(rightParam).Trim(true).Trim(false).IsEmpty();
		rightParam.Trim(true); rightParam.Trim(false);
		// A later pair of the same code wins, as it did when the pairs went into a map.
		const wxString value = allSpace ? wxString(wxT(" ")) : rightParam;
		bool replaced = false;
		for (auto& pair : pairs) {
			if (pair.first == leftParam) { pair.second = value; replaced = true; break; }
		}
		if (!replaced && !leftParam.IsEmpty())
			pairs.emplace_back(leftParam, value);
		bLeftParam = true; leftParam = ""; rightParam = "";
	};
	for (unsigned int i = 0; i < fmt.length(); i++) {
		auto c = fmt.at(i);
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

wxString ibFormatString::Render() const
{
	wxString text;
	const auto put = [&text](const wxString& name, const wxString& value) {
		if (!text.IsEmpty()) text += wxT("; ");
		text += name + wxT("=") + value;
	};

	// In the order of the tabs, each kind's codes in the order of the table; then what is only kept.
	for (const ibFormatCode code : Codes()) {
		if (const std::optional<wxString> value = Get(code))
			put(CodeName(code), *value);
	}
	for (const auto& pair : m_other)
		put(pair.first, pair.second);
	return text;
}

wxString ibFormatString::Apply(const ibAnyValue& value) const
{
	wxString result;
	Apply(value, result);
	return result;
}

bool ibFormatString::Apply(const ibAnyValue& value, wxString& result) const
{
	switch (value.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: {
		const std::optional<wxString>& said = value.GetBoolean() ? m_boolean.m_true : m_boolean.m_false;
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
			// DF first; DLF when there is no DF (as 1C: an explicit pattern wins).
			const ibDateNames& names = NamesFor(m_language);
			wxString pattern;
			if (m_date.m_pattern)
				pattern = *m_date.m_pattern;
			else if (m_date.m_localFormat)
				pattern = LocalFormatPattern(*m_date.m_localFormat, names);
			if (pattern.IsEmpty()) {
				result = value.GetString();
				return !result.IsEmpty();
			}
			ibDateTimeParts parts;
			value.GetDate().ToParts(parts);
			WriteDate(pattern, parts, names, result);
		}
		return !result.IsEmpty();
	default:
		result = value.GetString();   // every other type formats as its plain string
		return !result.IsEmpty();
	}
}
