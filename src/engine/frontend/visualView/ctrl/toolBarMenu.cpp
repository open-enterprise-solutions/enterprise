#include "toolBar.h"

// Designer-only context menu (right-click on a toolbar in the form
// editor): offers "Add tool" / "Add separator". Web has no designer
// editor — the virtuals stay on the vtable but collapse to no-ops.
#ifndef OES_USE_WEB
enum
{
	MENU_ADDITEM = 1000,
	MENU_ADDITEM_SEPARATOR,
};
#endif

void ibValueToolbar::PrepareDefaultMenu(wxMenu* menu)
{
#ifndef OES_USE_WEB
	menu->Append(MENU_ADDITEM, _("Add tool\tInsert"))->SetBitmap(ibValueToolBarItem::GetIconGroup());
	menu->Append(MENU_ADDITEM_SEPARATOR, _("Add separator\tInsert"))->SetBitmap(ibValueToolBarSeparator::GetIconGroup());
	menu->AppendSeparator();
#else
	(void)menu;
#endif
}

void ibValueToolbar::ExecuteMenu(ibVisualHost* visualHost, int id)
{
#ifndef OES_USE_WEB
	(void)visualHost;
	switch (id)
	{
	case MENU_ADDITEM:
		AddToolItem();
		break;
	case MENU_ADDITEM_SEPARATOR:
		AddToolSeparator();
		break;
	}
#else
	(void)visualHost;
	(void)id;
#endif
}