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
		return std::to_wtring(num);
#endif
	}

	inline wxString UIntToStr(unsigned int num) {
#if wxUSE_UNICODE
		return std::to_wstring(num);
#else 
		return std::to_wtring(num);
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

	inline wxString TrimLeft(const wxString& strSource, const wxUniChar& c = ' ') {
		wxString result(strSource);
		const size_t pos = result.find_first_not_of(c);
		if (pos == 0)
			return result;
		result.erase(0, pos);
		return result;
	}

	inline wxString TrimLeft(wxString& strSource, const wxUniChar& c = ' ') {
		const size_t pos = strSource.find_first_not_of(c);
		if (pos == 0)
			return strSource;
		strSource.erase(0, pos);
		return strSource;
	}

	inline wxString TrimRight(const wxString& strSource, const wxUniChar& c = ' ') {
		wxString result(strSource);
		const size_t pos = result.find_last_not_of(c) + 1;
		if (pos == result.Length())
			return result;
		result.erase(pos, result.Length() - pos);
		return strSource;
	}

	inline wxString TrimRight(wxString& strSource, const wxUniChar& c = ' ') {
		const size_t pos = strSource.find_last_not_of(c) + 1;
		if (pos == strSource.Length())
			return strSource;
		strSource.erase(pos, strSource.Length() - pos);
		return strSource;
	}

	inline wxString TrimAll(const wxString& strSource, const wxUniChar& c = ' ') {
		wxString result(strSource);
		(void)TrimLeft(result, c);
		(void)TrimRight(result, c);
		return result;
	}

	inline wxString TrimAll(wxString& strSource, const wxUniChar& c = ' ') {
		(void)TrimLeft(strSource, c);
		(void)TrimRight(strSource, c);
		return strSource;
	}

	inline wxString MakeUpper(const wxString& strSource) {
		wxString strRet(TrimAll(strSource));
		(void)strRet.MakeUpper();
		return strRet;
	}

	inline wxString MakeUpper(wxString& strSource) {
		wxString strRet(strSource);
		(void)TrimAll(strRet);
		(void)strRet.MakeUpper();
		return strRet;
	}

	inline bool CompareString(const wxString& lhs, const wxString& rhs) {
		return lhs.CmpNoCase(rhs) == 0;
	}

	/**
	* Returns true if the character is a white space character. This properly handles
	* extended ASCII characters.
	*/
	inline bool IsSpace(const wxUniChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isspace throws up if it is).
		return c > 0 && isspace(c);
	}

	/**
	 * Returns true if the character is a symbol. Symbols include all of the punctuation
	 * marks except _.
	 */
	inline bool IsSymbol(const wxUniChar& c) {
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (ispunct throws up if it is).
		return c > 0 && c != '_' && std::ispunct(c);
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
		return c >= '0' && c <= '9';
	}

	inline wxString GenerateSynonym(const wxString& systemName) {
		wxString newSynonym;
		for (unsigned int i = 0; i < systemName.length(); i++) {
			wxUniChar c = systemName[i];
			if (i == 0) {
				newSynonym += wxToupper(c);
			}
			else if (c >= 'A' && c <= 'Z' ||
				(c >= 'À' && c <= 'ß')) {
				newSynonym += ' ';
				newSynonym += wxTolower(c);
			}
			else {
				newSynonym += (i > 0 ?
					c : wxToupper(c)
					);
			}
		}
		return newSynonym;
	}

	inline int CheckCorrectName(const wxString& systemName) {
		for (unsigned int i = 0; i < systemName.length(); i++) {
			if (!((systemName[i] == '_') ||
				(systemName[i] >= 'A' && systemName[i] <= 'Z') || (systemName[i] >= 'a' && systemName[i] <= 'z') ||
				(systemName[i] >= 'À' && systemName[i] <= 'ß') || (systemName[i] >= 'à' && systemName[i] <= 'ÿ') ||
				(systemName[i] >= '0' && systemName[i] <= '9'))) {
				//wxMessageBox(wxT("You can enter only numbers, letters and the symbol \"_\""), wxT("Error entering value"));
				return i;
			}
		}
		return wxNOT_FOUND;
	}
}