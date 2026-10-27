////////////////////////////////////////////////////////////////////////////
//	Description : A point and a size as data — their text (geometry.h)
////////////////////////////////////////////////////////////////////////////

#include "geometry.h"

namespace {

// `a,b` — two whole numbers, as both texts are; anything else is refused.
bool ReadPair(const ibString& text, int& first, int& second)
{
	ibString rest;
	const ibString head = text.BeforeFirst(ibT(','), &rest);
	long a = 0, b = 0;
	if (!head.TrimAll().ToLong(&a) || !rest.TrimAll().ToLong(&b))
		return false;
	first = static_cast<int>(a);
	second = static_cast<int>(b);
	return true;
}

} // namespace

bool ibPoint::FromString(const ibString& text)
{
	int x = 0, y = 0;
	if (text.IsEmpty() || !ReadPair(text, x, y))
		return false;
	*this = ibPoint(x, y);
	return true;
}

ibString ibPoint::ToString() const
{
	return ibString::Format(ibT("%d,%d"), m_x, m_y);
}

bool ibSize::FromString(const ibString& text)
{
	int width = 0, height = 0;
	if (text.IsEmpty() || !ReadPair(text, width, height))
		return false;
	*this = ibSize(width, height);
	return true;
}

ibString ibSize::ToString() const
{
	return ibString::Format(ibT("%d,%d"), m_width, m_height);
}
