#include "toolbar.h"

enum
{
	MENU_ADDITEM = 1000,
	MENU_ADDITEM_SEPARATOR,
};

void CValueToolbar::PrepareDefaultMenu(wxMenu *m_menu)
{
	m_menu->Append(MENU_ADDITEM, wxT("Add tool\tInsert"));
	m_menu->Append(MENU_ADDITEM_SEPARATOR, wxT("Add separator\tInsert"));
	m_menu->AppendSeparator();
}

void CValueToolbar::ExecuteMenu(IVisualHost *visualHost, int id)
{
	switch (id)
	{
	case MENU_ADDITEM: 
		this->AddToolItem();	
		break;
	case MENU_ADDITEM_SEPARATOR: 
		this->AddToolSeparator(); 
		break;
	}
}