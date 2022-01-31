#include "stringUtils.h"
#include "typeconv.h"

#include <wx/ffile.h>
#include <wx/fontmap.h>
#include <wx/choicdlg.h>
#include <wx/arrstr.h>
#include <wx/filefn.h>
#include <wx/wfstream.h>

namespace StringUtils
{
	wxString IntToStr(int num)
	{
		wxString result;
		result.Printf(wxT("%d"), num);
		return result;
	}

	wxString GetSupportedEncodings(bool columnateWithTab, wxArrayString* array)
	{
		wxString result = wxEmptyString;
		unsigned int count = wxFontMapper::GetSupportedEncodingsCount();
		unsigned int max = 40;
		for (unsigned int i = 0; i < count; ++i)
		{
			wxFontEncoding encoding = wxFontMapper::GetEncoding(i);
			wxString name = wxFontMapper::GetEncodingName(encoding);
			unsigned int length = name.length();
			if (length > max)
			{
				max = length + 10;
			}
			if (columnateWithTab)
			{
				name = name.Pad((unsigned int)((max - length) / 8 + 1), wxT('\t'));
			}
			else
			{
				name = name.Pad(max - length);
			}
			name += wxFontMapper::GetEncodingDescription(encoding);
			if (NULL != array)
			{
				array->Add(name);
			}
			result += name;
			result += wxT("\n");
		}

		return result;
	}

	wxString TrimLeft(const wxString &Source, wxChar c)
	{
		wxString sRes(Source);
		unsigned int pos = sRes.find_first_not_of(c);
		if (pos == 0)
		{
			return sRes;
		}
		sRes.erase(0, pos);
		return sRes;
	}

	wxString TrimLeft(wxString &Source, wxChar c)
	{
		unsigned int pos = Source.find_first_not_of(c);
		if (pos == 0)
			return Source;
		Source.erase(0, pos);
		return Source;
	}

	wxString TrimRight(const wxString &Source, wxChar c)
	{
		wxString sRes(Source);
		unsigned int pos = sRes.find_last_not_of(c) + 1;
		if (pos == sRes.Length())
			return sRes;
		sRes.erase(pos, sRes.Length() - pos);
		return Source;
	}

	wxString TrimRight(wxString &Source, wxChar c)
	{
		unsigned int pos = Source.find_last_not_of(c) + 1;
		if (pos == Source.Length())
			return Source;
		Source.erase(pos, Source.Length() - pos);
		return Source;
	}

	wxString TrimAll(const wxString &Source, wxChar c)
	{
		wxString sRes = Source;
		sRes = TrimLeft(sRes, c);
		sRes = TrimRight(sRes, c);
		return sRes;
	}

	wxString TrimAll(wxString &Source, wxChar c)
	{
		TrimLeft(Source, c);
		TrimRight(Source, c);
		return Source;
	}

	wxString MakeUpper(const wxString &Source)
	{
		wxString sRet = TrimAll(Source);
		sRet.MakeUpper();
		return sRet;
	}

	wxString MakeUpper(wxString &Source)
	{
		wxString sRet(Source);
		TrimAll(sRet);
		sRet.MakeUpper();
		return sRet;
	}

	int CheckCorrectName(const wxString &systemName)
	{
		for (unsigned int i = 0; i < systemName.length(); i++) {
			if (!((systemName[i] == '_') ||
				(systemName[i] >= 'A' && systemName[i] <= 'Z') || (systemName[i] >= 'a' && systemName[i] <= 'z') ||
				(systemName[i] >= 'À' && systemName[i] <= 'ß') || (systemName[i] >= 'à' && systemName[i] <= 'ÿ') ||
				(systemName[i] >= '0' && systemName[i] <= '9')))
			{
				wxMessageBox(wxT("You can enter only numbers, letters and the symbol \"_\""), wxT("Error entering value"));
				return i;
			}
		}

		return wxNOT_FOUND;
	}

	wxString GenerateSynonym(const wxString &systemName)
	{
		wxString newSynonym;
		for (unsigned int i = 0; i < systemName.length(); i++) {

			wxUniChar c = systemName[i];

			if (c >= 'A' && c <= 'Z' ||
				(c >= 'À' && c <= 'ß'))
			{
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

	bool IsSymbol(char c)
	{
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (ispunct throws up if it is).
		return c > 0 && c != '_' && ispunct(c);
	}

	bool IsSpace(char c)
	{
		// In addition to the regular tests, we need to make sure this isn't
		// an extended ASCII character as well (isspace throws up if it is).
		return c > 0 && isspace(c);
	}

	bool IsDigit(char c)
	{
		return c >= '0' && c <= '9';
	}

	static void SkipWhitespace(wxInputStream& input, unsigned int& lineNumber)
	{
		char c;

		while (!input.Eof())
		{
			c = input.Peek();
			if (c == '\n')
			{
				++lineNumber;
			}
			else if (c == '-')
			{
				input.GetC();
				char c2 = input.Peek();
				if (c2 == '-')
				{
					// Lua single line comment.
					while (!input.Eof() && input.GetC() != '\n')
					{
					}
					++lineNumber;
					continue;
				}
			}
			else if (c == '/')
			{
				input.GetC();
				char c2 = input.Peek();
				if (c2 == '*')
				{
					// C++ block comment.
					input.GetC();
					while (!input.Eof())
					{
						c = input.GetC();
						if (c == '\n')
						{
							++lineNumber;
						}
						if (c == '*' && input.Peek() == '/')
						{
							input.GetC();
							break;
						}
					}
					continue;
				}
				else if (c2 == '/')
				{
					// C++ single line comment.
					while (!input.Eof() && input.GetC() != '\n')
					{
					}
					++lineNumber;
					continue;
				}
				else
				{
					input.Ungetch(c);
					break;
				}
			}
			if (!IsSpace(c))
			{
				break;
			}
			input.GetC();
		}

	}

	bool GetToken(wxInputStream& input, wxString& result, unsigned int& lineNumber)
	{
		result.Empty();

		SkipWhitespace(input, lineNumber);

		// Reached the end of the file.
		if (input.Eof())
		{
			return false;
		}

		char c = input.GetC();

		if (c == '\"')
		{

			// Quoted string, search for the end quote.

			do
			{
				result += c;
				c = input.GetC();
			} while (input.IsOk() && c != '\"');

			result += c;
			return true;

		}

		char n = input.Peek();

		if (IsDigit(c) || (c == '.' && IsDigit(n)) || (c == '-' && IsDigit(n)))
		{

			bool hasDecimal = false;

			while (!IsSpace(c))
			{

				result.Append(c);

				if (input.Eof())
				{
					return true;
				}

				c = input.Peek();

				if (!IsDigit(c) && c != '.')
				{
					return true;
				}

				input.GetC();

				if (c == '\n')
				{
					++lineNumber;
					return true;
				}

			}

		}
		else
		{

			if (IsSymbol(c))
			{
				result = c;
				return true;
			}

			while (!IsSpace(c) && !input.Eof())
			{

				result.Append(c);

				if (IsSymbol(input.Peek()))
				{
					break;
				}

				c = input.GetC();

				if (c == '\n')
				{
					++lineNumber;
					return true;
				}

			}

		}

		return true;

	}

	bool PeekToken(wxInputStream& input, wxString& result)
	{
		unsigned int lineNumber = 0;

		if (!GetToken(input, result, lineNumber))
		{
			return false;
		}

		input.Ungetch(result + " ", result.Length() + 1);
		return true;

	}

	wxFontEncoding GetEncodingFromUser(const wxString& message)
	{
		wxArrayString array;
		GetSupportedEncodings(false, &array);
		int selection = ::wxGetSingleChoiceIndex(message, _("Choose an Encoding"), array, wxTheApp->GetTopWindow());
		if (-1 == selection)
		{
			return wxFONTENCODING_MAX;
		}
		return wxFontMapper::GetEncoding(selection);
	}
}