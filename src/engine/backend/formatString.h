#ifndef __FORMAT_STRING_H__
#define __FORMAT_STRING_H__

////////////////////////////////////////////////////////////////////////////
// The FORMAT STRING — `NFD=2; NGS= ; BT=Yes` — as a value.
////////////////////////////////////////////////////////////////////////////
//
// What `Format(value, format)` reads, and what the format string constructor edits. The text is
// how a format is WRITTEN DOWN; what it IS is below: which codes there are (ibFormatCode), which
// values each applies to (ibFormatKind — the constructor's tabs) and one structure per kind with a
// field per code. The text is read once, in Parse, and written once, in Render; Apply is the
// function itself, so the constructor's sample line prints exactly what a running module prints.
//
// ⚠ AN ABSENT CODE AND AN EMPTY ONE ARE DIFFERENT, which is why every field is optional: no `NZ`
// prints a zero as a number, `NZ=` prints it as nothing at all.
//
// ⚠ A CODE THIS ENGINE DOES NOT READ IS KEPT, not dropped (m_other): somebody wrote it, and a window
// that rewrites the string must not lose it on the way through.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"

#include <optional>
#include <utility>
#include <vector>

class ibValue;

// Which values a code applies to.
enum class ibFormatKind {
	Number,
	Date,
	Boolean,
};

// Every code Format reads. The names they are written with are in the table behind CodeName.
enum class ibFormatCode {
	NumberDigits,              // ND  - total significant digits, trailing zeros trimmed
	NumberFractionDigits,      // NFD - digits after the point, always that many
	NumberDecimalSeparator,    // NDS - the character between the whole and the fraction
	NumberGroupSeparator,      // NGS - the character between digit groups (groups of three unless NG)
	NumberGroupSize,           // NG  - digits per group
	NumberZero,                // NZ  - what a zero prints as
	DatePattern,               // DF  - yyyy/yy year, mm/m month, dd/d day, HH/H hours, MM/M minutes, SS/S seconds
	DateEmpty,                 // DE  - what an empty date prints as
	BooleanTrue,               // BT  - what True prints as
	BooleanFalse,              // BF  - what False prints as
};

// The patterns a date pattern is usually one of. `Custom` is any other.
enum class ibDatePreset {
	Custom,
	Date,                      // dd.mm.yyyy
	DateTime,                  // dd.mm.yyyy HH:MM:SS
	Time,                      // HH:MM:SS
	HoursMinutes,              // HH:MM
	MonthYear,                 // mm.yyyy
	Year,                      // yyyy
	Sortable,                  // yyyy-mm-dd
};

struct BACKEND_API ibNumberFormat {
	std::optional<int>       m_digits;             // ND
	std::optional<int>       m_fractionDigits;     // NFD
	std::optional<wxUniChar> m_decimalSeparator;   // NDS
	std::optional<wxUniChar> m_groupSeparator;     // NGS
	std::optional<int>       m_groupSize;          // NG
	std::optional<wxString>  m_zero;               // NZ

	bool operator == (const ibNumberFormat& other) const;
};

struct BACKEND_API ibDateFormat {
	std::optional<wxString>  m_pattern;            // DF
	std::optional<wxString>  m_empty;              // DE

	bool operator == (const ibDateFormat& other) const;
};

struct BACKEND_API ibBooleanFormat {
	std::optional<wxString>  m_true;               // BT
	std::optional<wxString>  m_false;              // BF

	bool operator == (const ibBooleanFormat& other) const;
};

class BACKEND_API ibFormatString {
public:

	ibNumberFormat  m_number;
	ibDateFormat    m_date;
	ibBooleanFormat m_boolean;

	// The codes Format does not read, each as written - kept, and written back after the others.
	std::vector<std::pair<wxString, wxString>> m_other;

	// The text read and written: `CODE=value` pairs separated by `;`. A value that is all space is a
	// space (`NGS= ` groups digits with one), any other is trimmed.
	static ibFormatString Parse(const wxString& text);
	wxString Render() const;

	// What Format(value, this) prints: a number, a date and a boolean each by their own codes, any
	// other value as its plain string.
	wxString Apply(const ibValue& value) const;

	// A value that can be written at all: `;` ends a pair and `=` is not kept in one, so neither
	// can stand in a text.
	static bool IsWritable(const wxString& text);

	// ONE CODE AT A TIME — the door Parse and Render go through themselves, open to a caller that edits
	// the string by code (the MCP verb `format_string`). Get answers the value in the form it is written
	// in, and nothing when the code is absent; Set reads a value exactly as Parse does; Clear removes it.
	std::optional<wxString> Get(ibFormatCode code) const;
	void Set(ibFormatCode code, const wxString& value);
	void Clear(ibFormatCode code);

	// Every code, in the order of the tabs; a code by the name it is written with; its spelling; the
	// values it applies to.
	static const std::vector<ibFormatCode>& Codes();
	static bool FindCode(const wxString& name, ibFormatCode& code);
	static const wxChar* CodeName(ibFormatCode code);
	static ibFormatKind KindOf(ibFormatCode code);

	// A preset's pattern (empty for Custom), and the preset a pattern is (Custom when none).
	static wxString PresetPattern(ibDatePreset preset);
	static ibDatePreset PresetOf(const wxString& pattern);

	bool operator == (const ibFormatString& other) const;
	bool operator != (const ibFormatString& other) const { return !(*this == other); }
};

#endif
