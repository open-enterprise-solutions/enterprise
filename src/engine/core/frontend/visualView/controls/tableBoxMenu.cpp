#include "tableBox.h"

enum
{
	MENU_ADDCOLUMN = 1000
};

void CValueTableBox::PrepareDefaultMenu(wxMenu *m_menu)
{
	m_menu->Append(MENU_ADDCOLUMN, wxT("Add column\tInsert"));
	m_menu->AppendSeparator();
}

void CValueTableBox::ExecuteMenu(IVisualHost *visualHost, int id)
{
	switch (id)
	{
	case MENU_ADDCOLUMN:this->AddColumn(); break;
	}
}