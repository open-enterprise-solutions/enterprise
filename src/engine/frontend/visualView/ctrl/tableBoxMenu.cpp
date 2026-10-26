#include "tableBox.h"
#include "frontend/win/picture.h"   // ToBitmap / ToIcon — a picture drawn

enum
{
	MENU_ADD_COLUMN = 1000,
	MENU_ADD_COLUMN_GROUP
};

void ibValueModelTableBox::PrepareDefaultMenu(wxMenu *menu)
{
	menu->Append(MENU_ADD_COLUMN, _("Add column\tInsert"))->SetBitmap(ToIcon(ibValueModelTableBoxColumn::GetIconGroup()));
	menu->Append(MENU_ADD_COLUMN_GROUP, _("Add column group"))->SetBitmap(ToIcon(ibValueModelTableBoxColumnGroup::GetIconGroup()));
	menu->AppendSeparator();
}

void ibValueModelTableBox::ExecuteMenu(ibVisualHost *visualHost, int id)
{
	switch (id)
	{
	case MENU_ADD_COLUMN:
		AddColumn();
		break;
	case MENU_ADD_COLUMN_GROUP:
		AddColumnGroup();
		break;
	}
}

// (AddColumn / AddColumnGroup — the two commands above — live with the table itself in
//  tableBox.cpp, beside the rest of what it does to its own children.)
