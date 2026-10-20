////////////////////////////////////////////////////////////////////////////
//	Description : a constant of a query (queryLiteral.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLiteral.h"

void ibQueryLiteral::SetType(ibValueTypes type)
{
	m_held = ibAnyValuePtr<ibAnyValue>();
	m_text = ibLexValue(type);
}

bool ibQueryLiteral::SetBoolean(const wxString& text)
{
	m_held = ibAnyValuePtr<ibAnyValue>();
	return m_text.SetBoolean(text);
}

bool ibQueryLiteral::SetNumber(const wxString& text)
{
	m_held = ibAnyValuePtr<ibAnyValue>();
	return m_text.SetNumber(text);
}

bool ibQueryLiteral::SetDate(const wxString& text)
{
	m_held = ibAnyValuePtr<ibAnyValue>();
	return m_text.SetDate(text);
}

bool ibQueryLiteral::SetString(const wxString& text)
{
	m_held = ibAnyValuePtr<ibAnyValue>();
	return m_text.SetString(text);
}
