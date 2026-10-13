////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a chart box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueChartBox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_chartBox = new wxPanel(parent, wxID_ANY);
}

void ibValueChartBox::Update(const ibProtocolNode& node)
{
	UpdateWindow(m_chartBox, node);
}
