#ifndef __STRING_UTILS_H__
#define __STRING_UTILS_H__

#include "core/fstring.h"   // ibString — the engine's own names compare here too

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace stringUtils
{
	inline ibString IntToStr(int num) {
		return std::to_wstring(num);
	}

	inline ibString UIntToStr(unsigned int num) {
		return std::to_wstring(num);
	}

	inline int StrToInt(const ibString& str) {
		int result = 0;
		str.ToInt(&result);
		return result;
	}

	inline unsigned int StrToUInt(const ibString& str) {
		unsigned int result = 0;
		str.ToUInt(&result);
		return result;
	}

	inline ibString TrimLeft(const ibString& strSource, const ibChar& c = ibT(' ')) {
		ibString result(strSource);
		const size_t pos = result.find_first_not_of(c);
		if (pos == 0)
			return result;
		result.erase(0, pos);
		return result;
	}

	inline ibString TrimLeft(ibString& strSource, const ibChar& c = ibT(' ')) {
		const size_t pos = strSource.find_first_not_of(c);
		if (pos == 0)
			return strSource;
		strSource.erase(0, pos);
		return strSource;
	}

	inline ibString TrimRight(const ibString& strSource, const ibChar& c = ibT(' ')) {
		ibString result(strSource);
		const size_t pos = result.find_last_not_of(c) + 1;
		if (pos == result.Length())
			return result;
		result.erase(pos, result.Length() - pos);
		return result;
	}

	inline ibString TrimRight(ibString& strSource, const ibChar& c = ibT(' ')) {
		const size_t pos = strSource.find_last_not_of(c) + 1;
		if (pos == strSource.Length())
			return strSource;
		strSource.erase(pos, strSource.Length() - pos);
		return strSource;
	}

	inline ibString TrimAll(const ibString& strSource, const ibChar& c = ibT(' ')) {
		ibString result(strSource);
		(void)TrimLeft(result, c);
		(void)TrimRight(result, c);
		return result;
	}

	inline ibString TrimAll(ibString& strSource, const ibChar& c = ibT(' ')) {
		(void)TrimLeft(strSource, c);
		(void)TrimRight(strSource, c);
		return strSource;
	}

	inline ibString MakeUpper(const ibString& strSource) {
		ibString strRet(TrimAll(strSource));
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::towupper);
		return strRet;
	}

	inline ibString MakeUpper(ibString& strSource) {
		ibString strRet(strSource);
		(void)TrimAll(strRet);
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::towupper);
		return strRet;
	}

	// Two names, the same text — case folded unless asked not to. Asked of the string
	// itself, in ONE call: the text lives behind the facade (fstring.cpp), and reading it here would be
	// a call per buffer and per length on every comparison (measured 2026-09-26: method resolve +16%).
	//
	// ⚠ NO MIXED PAIR (wxString, ibString): the two convert into each other AND a literal converts into
	// both, so a mixed overload makes every `IsSameAs(name, wxT("x"))` ambiguous. A wx name meeting
	// an engine name is the engine string's own question: `name.IsSameAs(wxName, false)`.
	inline bool IsSameAs(const ibString& lhs, const ibString& rhs, bool case_sensitive = false) {
		return lhs.IsSameAs(rhs, case_sensitive);
	}

	/**
	* Returns true if the character is a white space character. This properly handles
	* extended ASCII characters.
	*/
	inline bool IsSpace(const ibChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isspace throws up if it is).
		return c > 0 && std::isspace(c);
	}

	/**
	 * Returns true if the character is a symbol. Symbols include all of the punctuation
	 * marks except _.
	 */
	inline bool IsSymbol(const ibChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (ispunct throws up if it is).
		return c > 0 && c != ibT('_') && std::ispunct(c);
	}

	/**
	* Returns true if the character is a print chars.
	*/
	inline bool IsWord(const ibChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isprint throws up if it is).
		return c > 0 && std::isprint(c);
	}

	/**
	* Returns truie if the character is a digit. Symbols include all of the punctuation
	* marks except _.
	*/
	inline bool IsDigit(const ibChar& c) {
		return std::isdigit(c);
	}

	inline ibString GenerateSynonym(const ibString& strSystemName) {
		ibString strSynonym;
		for (size_t i = 0; i < strSystemName.length(); i++) {
			const ibChar c = strSystemName[i];
			if (strSynonym.IsEmpty()) {
				if (c < 0x80) {
					strSynonym += ibToUpper(c);
				}
				else {
					strSynonym += c;
				}
			}
			else if ((c >= ibT('A') && c <= ibT('Z')) ||
				(c >= 0x0410 && c <= 0x042F)) {
				strSynonym += ibT(' ');
				if (c < 0x80) {
					strSynonym += ibToLower(c);
				}
				else if (c >= 0x0410 && c <= 0x042F) {
					// Cyrillic uppercase to lowercase: add 0x20
					strSynonym += ibChar(c + 0x20);
				}
				else {
					strSynonym += c;
				}
			}
			else {
				strSynonym += c;
			}
		}
		return strSynonym;
	}

	inline int CheckCorrectName(const ibString& systemName) {
		for (unsigned int i = 0; i < systemName.length(); i++) {
			if (!((systemName[i] == '_') ||
				(systemName[i] >= 'A' && systemName[i] <= 'Z') || (systemName[i] >= 'a' && systemName[i] <= 'z') ||
				(systemName[i] >= L'\u0410' && systemName[i] <= L'\u042F') || (systemName[i] >= L'\u0430' && systemName[i] <= L'\u044F') ||
				(systemName[i] >= '0' && systemName[i] <= '9'))) {
				//wxMessageBox(wxT("You can enter only numbers, letters and the symbol \"_\""), wxT("Error entering value"));
				return i;
			}
		}
		return wxNOT_FOUND;
	}
}

// ⭐ …AND THE ORDER THAT EQUALITY BELONGS TO — for a map whose names are matched case-insensitively (the
// lexer's keywords and #Defines, a document's parameters): the folding of stringUtils::IsSameAs, so
// the names it calls equal are one key, and a lookup is a find rather than a walk over every name.
// Shaped like IsSameAs for the same measured reasons (see there): the wide storage by reference, and a
// character that matches as it is needs no folding — names that share a long prefix compare at the price
// of the one character where they part.
//
// ⚠ READ OFF THE BUFFERS, and a LETTER is the only thing folded. A checked build makes every `[]` of a
// std::wstring a call, and its towupper updates the locale on every call; a composed sheet files some 400
// thousand links in a map of this order, named `Link_<row>_<col>`, which part at a digit — a tenth of the
// sheet's writing went here (stack samples 2026-09-12, Debug). A character below 0x80 that is no letter
// folds to itself in every locale, so it decides as it stands, and the answer is still the one IsSameAs gives.
struct ibCaseFoldLess {
	bool operator()(const ibString& lhs, const ibString& rhs) const noexcept {
		const ibChar* const l = lhs.c_str();
		const ibChar* const r = rhs.c_str();
		const size_t length = lhs.length() < rhs.length() ? lhs.length() : rhs.length();
		for (size_t idx = 0; idx < length; ++idx) {
			const ibChar c1 = l[idx];
			const ibChar c2 = r[idx];
			if (c1 == c2)
				continue;
			if (c1 < 0x80 && c2 < 0x80 && !IsLatinLetter(c1) && !IsLatinLetter(c2))
				return c1 < c2;
			const auto f1 = ::towupper(c1);
			const auto f2 = ::towupper(c2);
			if (f1 != f2)
				return f1 < f2;
		}
		return lhs.length() < rhs.length();   // one ran out first: the shorter is the lesser
	}

private:
	static bool IsLatinLetter(ibChar c) noexcept { return (c >= ibT('a') && c <= ibT('z')) || (c >= ibT('A') && c <= ibT('Z')); }
};

// …and the same order for the ENGINE'S OWN STRING — the index of a member table. A separate order
// rather than a transparent one, so a lookup never converts a key on every comparison; and one call
// into the string per comparison (CmpNoCase), whose folding is the one IsSameAs has — so the names
// this order calls one key are exactly those IsSameAs calls equal.
struct ibStringCaseFoldLess {
	bool operator()(const ibString& lhs, const ibString& rhs) const {
		return lhs.CmpNoCase(rhs) < 0;
	}
};

#endif