////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the layout of a view - cells of one size (Grid)
////////////////////////////////////////////////////////////////////////////

#include "layout.h"

void ibLayoutViewGrid::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_grid = new wxGridSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Rows)),
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Cols)), 0, 0);
}

void ibLayoutViewGrid::Update(const ibProtocolNode& node)
{
	m_grid->SetRows(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Rows)));
	m_grid->SetCols(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Cols)));
}

wxSizer* ibLayoutViewGrid::GetSizer() const
{
	return m_grid;
}
