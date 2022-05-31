#include "notebook.h"

enum
{
	MENU_ADDPAGE = 1000
};

void CValueNotebook::PrepareDefaultMenu(wxMenu *m_menu)
{
	m_menu->Append(MENU_ADDPAGE, wxT("Add page\tInsert"));
	m_menu->AppendSeparator();
}

void CValueNotebook::ExecuteMenu(IVisualHost *visualHost, int id)
{
	if (id == MENU_ADDPAGE) AddNotebookPage();
}
