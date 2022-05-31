////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchctrl.h"
#include "frontend/settings/xmlutility.h"

#include <wx/wx.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

wxBEGIN_EVENT_TABLE(CWatchCtrl, wxTreeListCtrl)
EVT_SIZE(CWatchCtrl::OnSize)
EVT_LIST_COL_END_DRAG(wxID_ANY, CWatchCtrl::OnColumnEndDrag)
wxEND_EVENT_TABLE()

CWatchCtrl::CWatchCtrl(wxWindow *parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxValidator &validator, const wxString& name)
	: wxTreeListCtrl(parent, id, pos, size, style, validator, name)
{
	AddColumn(_("Name"), 0, wxALIGN_LEFT);
	SetColumnEditable(0, true);
	AddColumn(_("Value"), 0, wxALIGN_LEFT);
	SetColumnEditable(1, false);
	AddColumn(_("Type"), 0, wxALIGN_LEFT);
	SetColumnEditable(2, false);

	m_columnSize[0] = 0.5f;
	m_columnSize[1] = 0.5f;
	m_columnSize[2] = -120;

	UpdateColumnSizes();
}

CWatchCtrl::~CWatchCtrl()
{
}

void CWatchCtrl::SetValueFont(const wxFont& font)
{
	m_valueFont = font;

	// Update all of the items.
	UpdateFont(GetRootItem());
}

void CWatchCtrl::UpdateFont(const wxTreeItemId &item)
{
	if (item.IsOk())
	{
		SetItemFont(item, m_valueFont);
		UpdateFont(GetNextSibling(item));

		wxTreeItemIdValue cookie;
		wxTreeItemId child = GetFirstChild(item, cookie);

		while (child.IsOk())
		{
			UpdateFont(child);
			child = GetNextChild(item, cookie);
		}
	}
}

void CWatchCtrl::UpdateColumnSizes()
{
	// We subtract two off of the size to avoid generating scroll bars on the window.
	int totalSize = GetClientSize().x - 2;

	int columnSize[3];
	GetColumnSizes(totalSize, columnSize);

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		SetColumnWidth(i, columnSize[i]);
	}
}

void CWatchCtrl::GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const
{
	int fixedSize = 0;

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] < 0.0f)
		{
			columnSize[i] = static_cast<int>(-m_columnSize[i]);
			fixedSize += columnSize[i];
		}
	}

	// Set the size of the proportional columns.
	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] >= 0.0f)
		{
			columnSize[i] = static_cast<int>(m_columnSize[i] * (totalSize - fixedSize) + 0.5f);
		}
	}

	// Make sure the total size is exactly correct by resizing the final column.
	for (unsigned int i = 0; i < s_numColumns - 1; ++i)
	{
		totalSize -= columnSize[i];
	}

	columnSize[s_numColumns - 1] = totalSize;
}

void CWatchCtrl::OnColumnEndDrag(wxListEvent& event)
{
	// Resize all of the columns to eliminate the scroll bar.
	int totalSize = GetClientSize().x;
	int column = event.GetColumn();

	int columnSize[s_numColumns];
	GetColumnSizes(totalSize, columnSize);

	int x = event.GetPoint().x;

	int newColumnSize = GetColumnWidth(column);
	int delta = columnSize[column] - newColumnSize;

	if (column + 1 < s_numColumns)
	{
		columnSize[column] = newColumnSize;
		columnSize[column + 1] += delta;
	}

	// Update the proportions of the columns.
	int fixedSize = 0;

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] < 0.0f)
		{
			m_columnSize[i] = -static_cast<float>(columnSize[i]);
			fixedSize += columnSize[i];
		}
	}

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] >= 0.0f)
		{
			m_columnSize[i] = static_cast<float>(columnSize[i]) / (totalSize - fixedSize);
		}
	}

	UpdateColumnSizes();
}

void CWatchCtrl::OnSize(wxSizeEvent& event)
{
	UpdateColumnSizes();
	event.Skip();
}