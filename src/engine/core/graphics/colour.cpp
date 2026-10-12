////////////////////////////////////////////////////////////////////////////
//	Description : A colour as data — its text (colour.h)
////////////////////////////////////////////////////////////////////////////

#include "colour.h"

bool ibColour::FromString(const ibString& text)
{
	if (text.IsEmpty() || text[0] == wxT('w') || text[0] == wxT('x'))
		return false;

	unsigned char channel[3] = { 0, 0, 0 };
	ibString rest = text;
	for (size_t i = 0; i < 3 && !rest.IsEmpty(); ++i) {
		long number = 0;
		if (rest.BeforeFirst(wxT(','), &rest).ToLong(&number) && number >= 0 && number <= 255)
			channel[i] = static_cast<unsigned char>(number);
	}

	*this = ibColour(channel[0], channel[1], channel[2]);
	return true;
}

ibString ibColour::ToString() const
{
	if (!IsOk())
		return ibString();
	return ibString::Format(wxT("%d,%d,%d"), m_red, m_green, m_blue);
}
