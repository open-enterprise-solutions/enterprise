#include "clientMenu.h"

ibClientMenu::Item& ibClientMenu::Append(ibDocCommand command)
{
	Item item;
	item.kind = ibClientMenuItemKind::Command;
	item.command = command;
	return m_items.emplace_back(std::move(item));
}

ibClientMenu::Item& ibClientMenu::Append(ibProtocol::ibSchema::ibKind schema, const ibString& title)
{
	Item item;
	item.kind = ibClientMenuItemKind::Schema;
	item.schema = schema;
	item.title = title;
	return m_items.emplace_back(std::move(item));
}

ibClientMenu::Item& ibClientMenu::Append(ibProtocol::ibSchema::ibKind schema, ibProtocol::ibSchema::ibCommand command, const ibString& title)
{
	Item& item = Append(schema, title);
	item.schemaCommand = command;
	return item;
}

ibClientMenu::Item& ibClientMenu::AppendSubMenu(const ibClientMenu& menu, const ibString& title)
{
	Item item;
	item.kind = ibClientMenuItemKind::Menu;
	item.title = title;
	item.items = menu.m_items;
	return m_items.emplace_back(std::move(item));
}

void ibClientMenu::AppendSeparator()
{
	m_items.push_back(Item());
}
