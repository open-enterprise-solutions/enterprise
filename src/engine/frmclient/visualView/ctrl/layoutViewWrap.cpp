////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the layout of a view - a row or a column that wraps (Wrap)
////////////////////////////////////////////////////////////////////////////

#include "layout.h"

void ibLayoutViewWrap::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	// Not wxWRAPSIZER_DEFAULT_FLAGS, as on the desktop: its wxEXTEND_LAST_ON_EACH_LINE stretches the last control of every
	// line to the line's end, whatever its own sizer item and MaximumSize say. Leading spaces are still dropped.
	m_wrap = new wxWrapSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)), wxREMOVE_LEADING_SPACES);
}

void ibLayoutViewWrap::Update(const ibProtocolNode& node)
{
	m_wrap->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, m_wrap->GetOrientation())));
}

wxSizer* ibLayoutViewWrap::GetSizer() const
{
	return m_wrap;
}
