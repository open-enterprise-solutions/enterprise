////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider-team
//	Description : property types 
////////////////////////////////////////////////////////////////////////////

#include "common/types.h"
#include "utils/stringUtils.h"

#include <wx/tokenzr.h>

///////////////////////////////////////////////////////////////////////////////

IntList::IntList(wxString value, bool absolute_value) : m_abs(absolute_value)
{
	SetList(value);
}

void IntList::Add(int value)
{
	m_ints.push_back(m_abs ? std::abs(value) : value);
}

void IntList::DeleteList()
{
	m_ints.erase(m_ints.begin(), m_ints.end());
}

void IntList::SetList(const wxString &str)
{
	DeleteList();
	wxStringTokenizer tkz(str, wxT(","));
	while (tkz.HasMoreTokens())
	{
		long value;
		wxString token;
		token = tkz.GetNextToken();
		token.Trim(true);
		token.Trim(false);

		if (token.ToLong(&value))
			Add((int)value);
	}
}

wxString IntList::ToString()
{
	wxString result;

	if (m_ints.size() > 0)
	{
		result = StringUtils::IntToStr(m_ints[0]);

		for (unsigned int i = 1; i < m_ints.size(); i++)
			result = result + wxT(",") + StringUtils::IntToStr(m_ints[i]);
	}

	return result;
}