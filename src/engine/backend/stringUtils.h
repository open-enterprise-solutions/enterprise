#pragma once

#include <wx/wx.h>
#include <wx/string.h>

class wxInputStream;
class wxString;
class wxArrayString;

namespace stringUtils
{
	inline wxString IntToStr(int num) {
#if wxUSE_UNICODE
		return std::to_wstring(num);
#else 
		return std::to_string(num);
#endif
	}

	inline wxString UIntToStr(unsigned int num) {
#if wxUSE_UNICODE
		return std::to_wstring(num);
#else 
		return std::to_string(num);
#endif
	}

	inline int StrToInt(const wxString& str) {
		int result = 0;
		str.ToInt(&result);
		return result;
	}

	inline unsigned int StrToUInt(const wxString& str) {
		unsigned int result = 0;
		str.ToUInt(&result);
		return result;
	}

	inline wxString TrimLeft(const wxString& strSource, const wxUniChar& c = wxT(' ')) {
		wxString result(strSource);
		const size_t pos = result.find_first_not_of(c);
		if (pos == 0)
			return result;
		result.erase(0, pos);
		return result;
	}

	inline wxString TrimLeft(wxString& strSource, const wxUniChar& c = wxT(' ')) {
		const size_t pos = strSource.find_first_not_of(c);
		if (pos == 0)
			return strSource;
		strSource.erase(0, pos);
		return strSource;
	}

	inline wxString TrimRight(const wxString& strSource, const wxUniChar& c = wxT(' ')) {
		wxString result(strSource);
		const size_t pos = result.find_last_not_of(c) + 1;
		if (pos == result.Length())
			return result;
		result.erase(pos, result.Length() - pos);
		return result;
	}

	inline wxString TrimRight(wxString& strSource, const wxUniChar& c = wxT(' ')) {
		const size_t pos = strSource.find_last_not_of(c) + 1;
		if (pos == strSource.Length())
			return strSource;
		strSource.erase(pos, strSource.Length() - pos);
		return strSource;
	}

	inline wxString TrimAll(const wxString& strSource, const wxUniChar& c = wxT(' ')) {
		wxString result(strSource);
		(void)TrimLeft(result, c);
		(void)TrimRight(result, c);
		return result;
	}

	inline wxString TrimAll(wxString& strSource, const wxUniChar& c = wxT(' ')) {
		(void)TrimLeft(strSource, c);
		(void)TrimRight(strSource, c);
		return strSource;
	}

	inline wxString MakeUpper(const wxString& strSource) {
		wxString strRet(TrimAll(strSource));
#ifdef wxUSE_UNICODE	
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::towupper);
#else
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::toupper);
#endif 
		return strRet;
	}

	inline wxString MakeUpper(wxString& strSource) {
		wxString strRet(strSource);
		(void)TrimAll(strRet);
#ifdef wxUSE_UNICODE	
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::towupper);
#else
		std::transform(strRet.begin(), strRet.end(), strRet.begin(), ::toupper);
#endif 
		return strRet;
	}

	// THE STRING'S OWN COMPARISON, not a hand-rolled loop over a copy of it.
	//
	// This used to materialise BOTH sides with ToStdWstring() — two heap copies
	// per comparison — then index them through .at() (a bounds check per
	// character) and fold each character with locale-aware ::towupper. The
	// disassembly of the hot translation units shows 38 calls to this function,
	// and it sits under name resolution in the compiler, so the copies were
	// being paid wherever a name is matched.
	//
	// wxString compares against its own storage and picks the right thing per
	// platform (wchar_t on MSW, UTF-8 elsewhere), which the hand-rolled version
	// could not: it forced a wide copy even where the string is not wide.
	//
	// ⚠ Case folding past ASCII moves from ::towupper to wxWidgets' own rule.
	// Both are locale-dependent, so neither is a fixed answer — see the note on
	// non-ASCII folding in tests/test_valueContainer.cpp.
	// 🛑 DO NOT "OPTIMISE" THIS — IT WAS TRIED AND MEASURED, 2026-08-15.
	//
	// Every part of the shape below is load-bearing, and two rewrites that each
	// looked like an improvement made it worse:
	//
	//  * `wxString::CmpNoCase` instead of the loop — loses BOTH early exits: the
	//    length check (this is an equality test, not an ordering one, so unequal
	//    lengths end it without reading a character) and the `c1 == c2` skip
	//    (matching characters need no case conversion, and in name resolution
	//    matching names match exactly). CmpNoCase must walk and must fold.
	//
	//  * Iterating the wxString directly, to avoid "the ToStdWstring() copies" —
	//    **+27% on ParserBench** (6224 -> 7879 us for 200 functions), +19% on
	//    inserts and probes, controls flat. A wxString iterator hands out
	//    wxUniChar, and building one per character costs more than what it saves.
	//
	// AND THERE IS NO COPY TO SAVE — the `const auto&` below is doing real work.
	// wxString::ToStdWstring returns `const std::wstring&` straight into m_impl
	// when the build stores wide internally (wxUSE_UNICODE_WCHAR — MSW), so the
	// reference binds to the string's own storage and nothing is allocated. On a
	// UTF-8 build it does convert, ONCE, which then buys O(1) indexing — the very
	// thing indexing the wxString itself would not give there. Drop the ampersand
	// and MSW starts copying for real.
	//
	// The disassembly counts 38 calls to this from the hot translation units, so
	// it is worth optimising — but not in either of those two directions.
	inline bool CompareString(const wxString& lhs, const wxString& rhs,
		bool case_sensitive = false) noexcept {

		const size_t length = lhs.length();
		if (length != rhs.length())
			return false;

#ifndef _WXSTRING_COMPARE_STRING_
		const auto& stl_lhs = lhs.ToStdWstring();
		const auto& stl_rhs = rhs.ToStdWstring();
#endif // !_WXSTRING_COMPARE_STRING_

		// THE TWO BRANCHES INDEX DIFFERENT TYPES, so they want different operators
		// — this is not an inconsistency to tidy up:
		//
		//   std::wstring — `[]` yields the character with no bounds check. The
		//     index is already bounded by `length`, checked against both strings
		//     above, so at()'s test can never fire; it only emits per character
		//     and pulls in an exception path (_Xran in the disassembly).
		//   wxString — `[]` hands back a wxUniCharRef, a PROXY that must be
		//     CONSTRUCTED, where at() yields the character outright. NOT measured:
		//     this branch is behind _WXSTRING_COMPARE_STRING_ and does not compile
		//     in the default build, so the argument is from the wxString API, not
		//     from a disassembly.
		for (unsigned int idx = 0; idx < length; idx++) {
#ifndef _WXSTRING_COMPARE_STRING_
			const auto& c1 = stl_lhs[idx];
			const auto& c2 = stl_rhs[idx];
#else
			const auto& c1 = lhs.at(idx);
			const auto& c2 = rhs.at(idx);
#endif
			if (!case_sensitive && c1 == c2)
				continue;
#ifdef wxUSE_UNICODE
			if (!case_sensitive && ::towupper(c1) != ::towupper(c2))
#else
			if (!case_sensitive && ::toupper(c1) != ::toupper(c2))
#endif
				return false;
			else if (case_sensitive && c1 != c2)
				return false;
		}

		return true;
	}

	/**
	* Returns true if the character is a white space character. This properly handles
	* extended ASCII characters.
	*/
	inline bool IsSpace(const wxUniChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isspace throws up if it is).
		return c > 0 && std::isspace(c);
	}

	/**
	 * Returns true if the character is a symbol. Symbols include all of the punctuation
	 * marks except _.
	 */
	inline bool IsSymbol(const wxUniChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (ispunct throws up if it is).
		return c > 0 && c != wxT('_') && std::ispunct(c);
	}

	/**
	* Returns true if the character is a print chars.
	*/
	inline bool IsWord(const wxUniChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isprint throws up if it is).
		return c > 0 && std::isprint(c);
	}

	/**
	* Returns truie if the character is a digit. Symbols include all of the punctuation
	* marks except _.
	*/
	inline bool IsDigit(const wxUniChar& c) {
		return std::isdigit(c);
	}

	inline wxString GenerateSynonym(const wxString& strSystemName) {
		wxString strSynonym;
		for (size_t i = 0; i < strSystemName.length(); i++) {
			const wxUniChar c = strSystemName[i];
			const wxUniChar::value_type wc = c.GetValue();
			if (strSynonym.IsEmpty()) {
				if (c.IsAscii()) {
					strSynonym += wxToupper(c);
				}
				else {
					strSynonym += c;
				}
			}
			else if ((wc >= wxT('A') && wc <= wxT('Z')) ||
				(wc >= 0x0410 && wc <= 0x042F)) {
				strSynonym += wxT(' ');
				if (c.IsAscii()) {
					strSynonym += wxTolower(c);
				}
				else if (wc >= 0x0410 && wc <= 0x042F) {
					// Cyrillic uppercase to lowercase: add 0x20
					strSynonym += wxUniChar(wc + 0x20);
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

	inline int CheckCorrectName(const wxString& systemName) {
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