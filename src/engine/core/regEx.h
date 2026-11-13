#ifndef _CORE_REGEX_H__
#define _CORE_REGEX_H__

// ibRegEx - a regular expression, what wxRegEx (wx/regex.h) was to the engine, over std::wregex: Compile a pattern,
// ask whether a text Matches it.
//
// The grammar is ECMAScript's (wx's was PCRE2's - the same for | [] . * + ? ^ $ \d \w and groups). A pattern that does
// not compile is an answer (Compile is false), not a line in the log. A text is searched LINE BY LINE: `.` does not
// cross a line anyway, `^` and `$` are a line's ends, and a long text taken as one subject is where std::regex's
// recursion runs out of stack.

#include <regex>

#include "core/fstring.h"

enum {
	ibRE_DEFAULT = 0,   // a letter's case matters
	ibRE_ICASE = 1      // it does not (by the C++ library's table: the Latin letters for certain)
};

class CORE_API ibRegEx {
public:
	ibRegEx() = default;
	explicit ibRegEx(const ibString& pattern, int flags = ibRE_DEFAULT) { Compile(pattern, flags); }

	bool Compile(const ibString& pattern, int flags = ibRE_DEFAULT);
	bool IsValid() const { return m_valid; }

	// Whether a line of `text` holds a match.
	bool Matches(const ibString& text) const;

private:
	std::wregex m_regex;
	bool        m_valid = false;
};

#endif
