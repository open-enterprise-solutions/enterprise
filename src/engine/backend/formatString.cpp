////////////////////////////////////////////////////////////////////////////
//	Description : The format string as a value (formatString.h)
////////////////////////////////////////////////////////////////////////////

#include "formatString.h"

#include "backend/compiler/value.h"
#include "backend/fnumber.h"

#include <wx/datetime.h>

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
};

struct ibDatePresetInfo {
	ibDatePreset  m_preset;
	const wxChar* m_pattern;
};

const ibDatePresetInfo s_presets[] = {
	{ ibDatePreset::Date,         wxT("dd.mm.yyyy")          },
	{ ibDatePreset::DateTime,     wxT("dd.mm.yyyy HH:MM:SS") },
	{ ibDatePreset::Time,         wxT("HH:MM:SS")            },
	{ ibDatePreset::HoursMinutes, wxT("HH:MM")               },
	{ ibDatePreset::MonthYear,    wxT("mm.yyyy")             },
	{ ibDatePreset::Year,         wxT("yyyy")                },
	{ ibDatePreset::Sortable,     wxT("yyyy-mm-dd")          },
};

std::optional<wxUniChar> FirstChar(const wxString& value)
{
	if (value.IsEmpty())
		return std::nullopt;   // no character named: the number's own separator stands
	return value[0];
}

// The letters of a date pattern: year, month, day, hours, minutes, seconds. ⚠ `mm` IS THE MONTH and `MM`
// the minutes.
int DateLetter(wxUniChar c)
{
	switch (c.GetValue()) {
	case L'y': return 0;
	case L'm': return 1;
	case L'd': return 2;
	case L'H': return 3;
	case L'M': return 4;
	case L'S': return 5;
	}
	return -1;
}

void AppendTwoDigits(int value, wxString& out)
{
	out += static_cast<wxChar>(L'0' + value / 10 % 10);
	out += static_cast<wxChar>(L'0' + value % 10);
}

// ⭐ THE DATE PATTERN WRITTEN STRAIGHT INTO `out` — no strftime pattern is built and wxDateTime::Format is
// not called, because both allocate once per date and a report prints a date per row.
//
// The reading is the one the strftime translation gave (yyyy the full year, yyy / yy / y two digits,
// mm / m, dd / d, HH / H, MM / M, SS / S two digits each): a letter is taken by the LONGEST spelling the
// pattern uses anywhere, every run of it is read in pieces of that length, and what is left over stands as
// the letter — `yyyyy` is the year and a `y`. Every other character stands as it is.
void WriteDate(const wxString& pattern, const wxDateTime::Tm& tm, wxString& out)
{
	static const int s_longest[6] = { 4, 2, 2, 2, 2, 2 };
	const size_t length = pattern.length();

	int spelling[6] = { 0, 0, 0, 0, 0, 0 };
	for (size_t i = 0; i < length;) {
		size_t run = 1;
		while (i + run < length && pattern[i + run] == pattern[i]) ++run;
		const int letter = DateLetter(pattern[i]);
		if (letter >= 0)
			spelling[letter] = std::max(spelling[letter], std::min(static_cast<int>(run), s_longest[letter]));
		i += run;
	}

	out.clear();
	out.reserve(length + 4);
	for (size_t i = 0; i < length;) {
		const wxUniChar c = pattern[i];
		size_t run = 1;
		while (i + run < length && pattern[i + run] == c) ++run;
		const int letter = DateLetter(c);
		size_t at = 0;
		if (letter >= 0) {
			const size_t piece = static_cast<size_t>(spelling[letter]);
			for (; at + piece <= run; at += piece) {
				switch (letter) {
				case 0:
					if (piece == 4 && tm.year >= 0 && tm.year <= 9999) {
						AppendTwoDigits(tm.year / 100, out);
						AppendTwoDigits(tm.year % 100, out);
					}
					else if (piece == 4)
						out << tm.year;
					else
						AppendTwoDigits((tm.year % 100 + 100) % 100, out);
					break;
				case 1: AppendTwoDigits(tm.mon + 1, out); break;
				case 2: AppendTwoDigits(tm.mday, out);    break;
				case 3: AppendTwoDigits(tm.hour, out);    break;
				case 4: AppendTwoDigits(tm.min, out);     break;
				case 5: AppendTwoDigits(tm.sec, out);     break;
				}
			}
		}
		if (at < run)
			out.append(run - at, c);
		i += run;
	}
}

} // namespace

bool ibNumberFormat::operator == (const ibNumberFormat& other) const
{
	return m_digits == other.m_digits && m_fractionDigits == other.m_fractionDigits
		&& m_decimalSeparator == other.m_decimalSeparator && m_groupSeparator == other.m_groupSeparator
		&& m_groupSize == other.m_groupSize && m_zero == other.m_zero;
}

bool ibDateFormat::operator == (const ibDateFormat& other) const
{
	return m_pattern == other.m_pattern && m_empty == other.m_empty;
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

bool ibFormatString::FindCode(const wxString& name, ibFormatCode& code)
{
	for (const ibFormatCodeInfo& info : s_codes) {
		if (name == info.m_name) { code = info.m_code; return true; }
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

wxString ibFormatString::Apply(const ibValue& cData) const
{
	wxString result;
	Apply(cData, result);
	return result;
}

bool ibFormatString::Apply(const ibValue& cData, wxString& result) const
{
	switch (cData.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: {
		const std::optional<wxString>& text = cData.GetBoolean() ? m_boolean.m_true : m_boolean.m_false;
		if (text)
			result = *text;
		else
			result = cData.GetString();
		break;
	}
	case ibValueTypes::TYPE_NUMBER: {
		const ibNumber number = cData.GetNumber();

		// NZ: replacement string when value is exactly zero.
		if (number.IsZero() && m_number.m_zero) {
			result = *m_number.m_zero;
			break;
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
		break;
	}
	case ibValueTypes::TYPE_DATE: {
		if (cData.IsEmpty() && m_date.m_empty) {
			result = *m_date.m_empty;
			break;
		}
		if (m_date.m_pattern) {
			const wxDateTime dateTime = wxLongLong(cData.GetDate());
			WriteDate(*m_date.m_pattern, dateTime.GetTm(), result);
			break;
		}
		result = cData.GetString();
		break;
	}
	default:
		result = cData.GetString();   // every other type formats as its plain string
		break;
	}

	return !result.IsEmpty();
}
