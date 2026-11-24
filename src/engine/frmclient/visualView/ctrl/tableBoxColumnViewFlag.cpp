////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a table column's Flag — its cells a check box
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"

#include <wx/renderer.h>   // wxCONTROL_CHECKED — a flag's box

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"   // ibThemeEngine — a flag's box, as the theme draws a check box

bool ibViewCellRendererFlag::SetValue(const ibVariant& value)
{
	m_flag.reset();
	if (value.GetType() == ibValueTypes::TYPE_BOOLEAN)
		m_flag = value.GetBoolean();
	return true;
}

bool ibViewCellRendererFlag::Render(wxRect rect, wxDC* dc, int state)
{
	if (!m_flag.has_value())
		return true;
	int flags = *m_flag ? wxCONTROL_CHECKED : 0;
	if (state & wxDATAVIEW_CELL_PRELIT)
		flags |= wxCONTROL_CURRENT;
	// The theme's box, as ibCheckBox draws it.
	ibThemeEngine::Get()->GetRenderer()->DrawCheckBox(GetView(), *dc, BoxIn(rect), flags);
	return true;
}

wxSize ibViewCellRendererFlag::GetSize() const
{
	return ibThemeEngine::Get()->GetRenderer()->GetCheckBoxSize(GetView());
}

bool ibViewCellRendererFlag::ActivateCell(const wxRect& cell, ibDataViewModel* /*model*/, const ibDataViewItem& item,
	unsigned int col, const wxMouseEvent* mouseEvent)
{
	// A click beside the box is the row's, not the flag's (the mouse's position is the cell's own).
	if (mouseEvent != nullptr && !BoxIn(wxRect(cell.GetSize())).Contains(mouseEvent->GetPosition()))
		return false;
	ibValueModelTableBox* const table = m_column.GetOwner();
	if (table == nullptr || !m_flag.has_value() || !ibViewTableModel::IsCellEditable(item, col))
		return false;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Checked, !*m_flag);
	table->SendCell(ibProtocol::ibEvent::ibKind::Toggle, ibViewTableModel::HandleOf(item), col, args);
	return true;
}

wxRect ibViewCellRendererFlag::BoxIn(const wxRect& rect) const
{
	return wxRect(rect.GetPosition(), GetSize()).CentreIn(rect, wxVERTICAL);
}
