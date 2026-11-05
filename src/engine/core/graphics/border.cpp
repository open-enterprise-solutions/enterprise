#include "border.h"

bool ibBorder::FromString(const ibString& text)
{
	const int comma = text.Find(ibT(','));
	if (comma == wxNOT_FOUND)
		return false;
	int kind = 0, width = 0;
	if (!text.Left(comma).Strip(ibString::both).ToInt(&kind) || !text.Mid(comma + 1).Strip(ibString::both).ToInt(&width))
		return false;
	if (kind < static_cast<int>(ibBorderKind::None) || kind > static_cast<int>(ibBorderKind::Rounded) || width < 0
		|| width > kMaxWidth)
		return false;
	m_kind = static_cast<ibBorderKind>(kind);
	m_width = width;
	return true;
}

ibString ibBorder::ToString() const
{
	return ibString::Format(ibT("%d,%d"), static_cast<int>(m_kind), m_width);
}
