#ifndef __DOC_COMMAND_H__
#define __DOC_COMMAND_H__

// THE COMMANDS A CLIENT'S DOCUMENTS ANSWER TO — what the desktop's doc manager took from its menu and its keys
// (wxID_UNDO, wxID_SAVE, …), done on the active tab's document — and what the desktop's main frame took there besides
// (wxID_OPEN, wxID_EXIT, the edit commands). A number on the wire, a type here: never renumbered, a new command takes
// the next one. Which of them an application has, with what name and key, its doc manager registers
// (ibDocManager::GetCommands).
//
// EVERY ONE GOES TO THE SERVER, the ones whose subject is the client's too: the server decides when it may be done,
// and what is the client's it asks of the client (ibClientFrame::DoCommand) — the field under the focus edited, a
// file of the person's chosen.

#include <wx/string.h>

#include "core/fileSystem/types.h"   // s32

enum class ibDocCommand : s32 {
	Undo      = 1,    // the document's last command undone (its command processor)
	Redo      = 2,    // …and done again
	Save      = 3,    // the document saved — a form's object written
	Close     = 4,    // its tab closed
	Open      = 5,    // a file of the person's chosen, and opened by the template its name says (a File request)
	Exit      = 6,    // every tab closed — each may keep itself, asking about what is unsaved — and the client gone
	Cut       = 7,    // the field under the client's focus edited (an Edit request) — wx's wxID_CUT
	Copy      = 8,    // …wxID_COPY
	Paste     = 9,    // …wxID_PASTE
	Delete    = 10,   // …wxID_DELETE
	SelectAll = 11,   // …wxID_SELECTALL
	SaveAs    = 12,   // the document saved as a file of the name the client chose (Name) — in the session's temporary
	                  // storage, its tab's File, taken down by the client to where the person keeps it
};

// THE PICTURE A COMMAND IS SHOWN WITH — one of the client's own stock pictures (the desktop's wxArtProvider ids, which
// its art provider draws), named: a client draws its platform's, as it maps a key to its platform's. One command,
// one picture, wherever it is pressed from — the menu and the toolbar alike. A number on the wire, never renumbered.
enum class ibClientStockPicture : s32 {
	None   = 0,    // shown by its title alone
	New    = 1,    // wxART_NEW
	Open   = 2,    // wxART_FILE_OPEN
	Save   = 3,    // wxART_FILE_SAVE
	SaveAs = 4,    // wxART_FILE_SAVE_AS
	Close  = 5,    // wxART_CLOSE
	Quit   = 6,    // wxART_QUIT
	Undo   = 7,    // wxART_UNDO
	Redo   = 8,    // wxART_REDO
	Cut    = 9,    // wxART_CUT
	Copy   = 10,   // wxART_COPY
	Paste  = 11,   // wxART_PASTE
	Delete = 12,   // wxART_DELETE
	Find   = 13,   // wxART_FIND
	Print  = 14,   // wxART_PRINT
};

// What a client is told of a command: its name, the key that gives it — modifiers Ctrl / Alt / Shift and the key,
// "Ctrl+Z"; a client maps them to its own platform's (Ctrl to Cmd on a Mac) — and its picture.
struct ibDocCommandInfo {
	wxString             title;
	wxString             shortcut;
	ibClientStockPicture picture = ibClientStockPicture::None;
};

#endif
