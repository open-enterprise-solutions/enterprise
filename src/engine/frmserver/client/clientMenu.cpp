#include "clientMenu.h"

ibClientMenu::Item& ibClientMenu::Append(ibDocCommand command)
{
	Item item;
	item.kind = ibClientMenuItemKind::Command;
	item.command = command;
	m_items.push_back(std::move(item));
	return m_items.back();
}

ibClientMenu::Item& ibClientMenu::Append(ibProtocolSchema schema, const wxString& title)
{
	Item item;
	item.kind = ibClientMenuItemKind::Schema;
	item.schema = schema;
	item.title = title;
	m_items.push_back(std::move(item));
	return m_items.back();
}

ibClientMenu::Item& ibClientMenu::AppendSubMenu(const ibClientMenu& menu, const wxString& title)
{
	Item item;
	item.kind = ibClientMenuItemKind::Menu;
	item.title = title;
	item.items = menu.m_items;
	m_items.push_back(std::move(item));
	return m_items.back();
}

void ibClientMenu::AppendSeparator()
{
	m_items.push_back(Item());
}
