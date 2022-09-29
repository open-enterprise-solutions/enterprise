#include "tableBox.h"

enum
{
	MENU_ADD_COLUMN = 1000
};

void CValueTableBox::PrepareDefaultMenu(wxMenu *menu)
{
	menu->Append(MENU_ADD_COLUMN, _("Add column\tInsert"));
	menu->AppendSeparator();
}

void CValueTableBox::ExecuteMenu(IVisualHost *visualHost, int id)
{
	switch (id)
	{
	case MENU_ADD_COLUMN: AddColumn();
		break;
	}
}