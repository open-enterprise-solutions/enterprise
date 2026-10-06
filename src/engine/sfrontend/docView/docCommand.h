#ifndef __DOC_COMMAND_H__
#define __DOC_COMMAND_H__

// THE COMMANDS A CLIENT'S DOCUMENTS ANSWER TO — what the desktop's doc manager took from its menu and its keys
// (wxID_UNDO, wxID_SAVE, …), done on the active tab's document. A number on the wire, a type here: never
// renumbered, a new command takes the next one. Which of them an application has, with what name and key, its
// doc manager registers (ibDocManager::GetCommands).

#include <wx/string.h>

#include "backend/fileSystem/types.h"   // s32

enum class ibDocCommand : s32 {
	Undo  = 1,   // the document's last command undone (its command processor)
	Redo  = 2,   // …and done again
	Save  = 3,   // the document saved — a form's object written
	Close = 4,   // its tab closed
};

// What a client is told of a command: its name, and the key that gives it — modifiers Ctrl / Alt / Shift and the
// key, "Ctrl+Z"; a client maps them to its own platform's (Ctrl to Cmd on a Mac).
struct ibDocCommandInfo {
	wxString title;
	wxString shortcut;
};

#endif
