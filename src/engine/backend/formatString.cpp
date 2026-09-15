////////////////////////////////////////////////////////////////////////////
//	Description : The format string as a value (formatString.h)
////////////////////////////////////////////////////////////////////////////

#include "formatString.h"

#include "backend/compiler/value.h"
#include "backend/fnumber.h"

#include <wx/datetime.h>

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

// The date pattern in wxDateTime's terms. ⚠ `mm` IS THE MONTH and `MM` the minutes.
wxString ToStrftime(const wxString& pattern)
{
	wxString newFormat = pattern;

	//year
	if (newFormat.Replace("yyyy", "%Y") == 0) {
		if (newFormat.Replace("yyy", "%y") == 0) {
			if (newFormat.Replace("yy", "%y") == 0) {
				newFormat.Replace("y", "%y");
			}
		}
	}

	//month
	if (newFormat.Replace("mm", "%m") == 0) {
		newFormat.Replace("m", "%m");
	}

	//day
	if (newFormat.Replace("dd", "%d") == 0) {
		newFormat.Replace("d", "%d");
	}

	//hour
	if (newFormat.Replace("HH", "%H") == 0) {
		newFormat.Replace("H", "%H");
	}

	//minute
	if (newFormat.Replace("MM", "%M") == 0) {
		newFormat.Replace("M", "%M");
	}

	//second
	if (newFormat.Replace("SS", "%S") == 0) {
		newFormat.Replace("S", "%S");
	}

	return newFormat;
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
	return format;
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
	switch (cData.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: {
		const std::optional<wxString>& text = cData.GetBoolean() ? m_boolean.m_true : m_boolean.m_false;
		if (text)
			return *text;
		return cData.GetString();
	}
	case ibValueTypes::TYPE_NUMBER: {
		const ibNumber number = cData.GetNumber();

		// NZ: replacement string when value is exactly zero.
		if (number.IsZero() && m_number.m_zero)
			return *m_number.m_zero;

		ibNumber::Format numFmt;
		if (m_number.m_fractionDigits)   numFmt.fracDigits = *m_number.m_fractionDigits;
		if (m_number.m_digits)           numFmt.precision  = *m_number.m_digits;
		if (m_number.m_decimalSeparator) numFmt.decimalSep = *m_number.m_decimalSeparator;
		if (m_number.m_groupSeparator) {
			numFmt.groupSep  = *m_number.m_groupSeparator;
			numFmt.groupSize = 3;   // a separator names no group size of its own: thousands, unless NG says
		}
		if (m_number.m_groupSize)        numFmt.groupSize  = *m_number.m_groupSize;

		return number.ToString(numFmt);
	}
	case ibValueTypes::TYPE_DATE: {
		if (cData.IsEmpty() && m_date.m_empty)
			return *m_date.m_empty;

		if (m_date.m_pattern) {
			const wxDateTime dateTime = wxLongLong(cData.GetDate());
			return dateTime.Format(ToStrftime(*m_date.m_pattern));
		}
		return cData.GetString();
	}
	default:
		break;      // every other type formats as its plain string
	}

	return cData.GetString();
}
