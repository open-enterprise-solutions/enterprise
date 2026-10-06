#ifndef __CLIENT_MENU_H__
#define __CLIENT_MENU_H__

// THE CLIENT'S MENU — wx's menu (wxMenuBar, wxMenu, wxMenuItem) made data: what the desktop's main frame put in its
// menu bar, which a client draws as it likes. Built by the frame of the client's mode, as the desktop's
// InitializeDefaultMenu built it (ibClientFrameRuntime, ibClientFrameDesigner). An item does one of the things the
// protocol already does — a command of the documents (`command`), or a schema the client shows (`schema`) — or
// opens a menu of its own; its name and key are the command's own, as the doc manager registered it. Whether an
// item is enabled is not kept here: it is asked as the frame is drawn — of the document a command is done on, of
// the schema's own right — the same questions a call of it is refused by.

#include <vector>

#include <wx/string.h>

#include "sfrontend/docView/docCommand.h"
#include "sfrontend/schema/clientSchema.h"
#include "sfrontend/sfrontend.h"

// What a menu item is — wx's item kinds, those a server menu has, each with what it does.
enum class ibClientMenuItemKind : s32 {
	Separator = 0,   // a line between groups
	Command   = 1,   // one of the documents' commands — done by the server (`command`)
	Schema    = 2,   // a schema — asked for and shown by the client (`schema`)
	Menu      = 3,   // a menu of its own
};

class SFRONTEND_API ibClientMenu {
public:

	struct Item {
		ibClientMenuItemKind kind = ibClientMenuItemKind::Separator;
		ibDocCommand         command{};   // a command's
		ibClientSchemaKind   schema{};    // a schema's
		wxString             title;       // a schema's and a menu's — a command's is its own
		std::vector<Item>    items;       // a menu's
	};

	Item& Append(ibDocCommand command);
	Item& Append(ibClientSchemaKind schema, const wxString& title);
	Item& AppendSubMenu(const ibClientMenu& menu, const wxString& title);
	void  AppendSeparator();

	const std::vector<Item>& GetItems() const { return m_items; }
	bool IsEmpty() const { return m_items.empty(); }

private:

	std::vector<Item> m_items;
};

#endif
