////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the layout of a view - a row or a column (Box)
////////////////////////////////////////////////////////////////////////////

#include "layout.h"

void ibLayoutViewBox::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_box = new wxBoxSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
}

void ibLayoutViewBox::Update(const ibProtocolNode& node)
{
	m_box->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, m_box->GetOrientation())));
}

wxSizer* ibLayoutViewBox::GetSizer() const
{
	return m_box;
}
