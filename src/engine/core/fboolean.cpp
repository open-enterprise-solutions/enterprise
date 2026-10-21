#include "core/fboolean.h"

#include "core/fstring.h"

bool ibBoolean::FromString(const ibString& text)
{
	const ibString word = text.TrimAll();
	m_flag = word.IsSameAs(g_booleanTrue, false);
	return m_flag || word.IsSameAs(g_booleanFalse, false);
}

ibString ibBoolean::ToString() const
{
	return m_flag ? g_booleanTrue : g_booleanFalse;
}
