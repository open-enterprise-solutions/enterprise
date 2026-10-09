#ifndef __CHOICE_REQUEST_H__
#define __CHOICE_REQUEST_H__

// A CHOICE PUT TO THE PERSON — one of a list, through the current session's frame (ibBackendDocFrame::Request,
// ibProtocolRequestKind::Choice: a caption, and one child per item with its Id, Caption, Picture and whether it is
// Selected now; the response's Id names the item chosen). The one shape for every "pick one of these" a form on the
// server makes: a quick choice, a type among several, the columns a list is printed with.

#include <vector>

#include <wx/string.h>

#include "frmserver/frmserver.h"
#include "backend/backend_picture.h"    // ibServerPicture — the item's picture as it is sent
#include "core/fileSystem/types.h"   // s32

struct ibChoiceItem {
	s32             id = 0;
	wxString        caption;
	ibServerPicture icon;
	bool            selected = false;
};

// False: nothing chosen — cancelled, or nobody to ask. Only what was offered can come back: a response naming
// an Id that is not among `items` is no choice at all.
FRMSERVER_API bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, s32& chosen);
// …and SEVERAL of a list (Multiple → Ids), those `selected` ticked to begin with. False: none chosen — cancelled, none
// ticked, or nobody to ask.
FRMSERVER_API bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, std::vector<s32>& chosen);

// A POPUP MENU AT THE CURSOR — the desktop's wxMenu shown by GetPopupMenuSelectionFromUser, put to the person through
// the session's frame (ibProtocolRequestKind::Menu): an item's id and caption, its tick, greyed out, and its own items — a
// submenu. A plain structure, not a wxMenu: a menu is a window's, and the server's code runs on a worker.
struct ibMenuItem {
	s32                     id = 0;
	wxString                caption;
	bool                    checked = false;
	bool                    enabled = true;
	std::vector<ibMenuItem> items;   // a submenu — the item itself is then no command
};

// The id of the item picked. False: dismissed, or nobody to ask; only an id offered and enabled can come back.
FRMSERVER_API bool ibRequestMenu(const std::vector<ibMenuItem>& items, s32& chosen);

#endif
