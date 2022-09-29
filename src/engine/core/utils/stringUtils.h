#pragma once

#include <wx/string.h>

class wxInputStream;
class wxString;
class wxArrayString;

namespace StringUtils
{
	wxString IntToStr(signed int num);
	wxString UIntToStr(unsigned int num);

	wxString TrimLeft(const wxString &Source, wxChar c = ' ');
	wxString TrimLeft(wxString &Source, wxChar c = ' ');
	wxString TrimRight(const wxString &Source, wxChar c = ' ');
	wxString TrimRight(wxString &Source, wxChar c = ' ');
	wxString TrimAll(const wxString &Source, wxChar c = ' ');
	wxString TrimAll(wxString &Source, wxChar c = ' ');

	wxString MakeUpper(const wxString &Source);
	wxString MakeUpper(wxString &Source);

	int CheckCorrectName(const wxString &systemName);
	wxString GenerateSynonym(const wxString &systemName);

	inline bool CompareString(const wxString &lhs, const wxString &rhs)
	{
		return lhs.CompareTo(rhs, wxString::ignoreCase) == 0;
	}

	/**
	* Returns true if the character is a white space character. This properly handles
	* extended ASCII characters.
	*/
	bool IsSpace(char c);

	/**
	 * Returns truie if the character is a symbol. Symbols include all of the punctuation
	 * marks except _.
	 */
	bool IsSymbol(char c);


	/**
	* Returns truie if the character is a digit. Symbols include all of the punctuation
	* marks except _.
	*/
	bool IsDigit(char c);

	/**
	* Reads a token from the input stream and stores it in result. If the end of the
	* stream was reached before anything was read, the function returns false.
	*/
	bool GetToken(wxInputStream& input, wxString& result, unsigned int& lineNumber);

	/**
	 * Reads a token from the input stream and stores it in result without actually pulling
	 * the token from the stream. If the end of the stream was reached before anything was
	 * read, the function returns false.
	 */
	bool PeekToken(wxInputStream& input, wxString& result);

	wxString GetSupportedEncodings(bool columnateWithTab = true, wxArrayString* array = NULL);
	wxFontEncoding GetEncodingFromUser(const wxString& message);
}