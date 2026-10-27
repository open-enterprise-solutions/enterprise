#ifndef __SETTINGS_SHELF_H__
#define __SETTINGS_SHELF_H__

////////////////////////////////////////////////////////////////////////////
//	Description : A PERSON'S SHELF OF SETTINGS - what it is kept under, and one entry it shows
////////////////////////////////////////////////////////////////////////////
//
// The shelf is the server's (backend/settings/settingsStorage.h keeps the rows, settingsComposer.h reads
// a composer's into them); its window — the desktop's beside it, the client's through the question the
// server waits on — names the category and lists the entries. One vocabulary for both, so it lives here.

#include "core/guid.h"        // ibGuid — an entry's identity
#include "core/fdatetime.h"   // ibDateTime — when it was written


// WHAT KIND OF SETTING THIS IS — the tenant, not a folder name. It is part of
// the key, so one category's "MainForm" and another's are different rows.
//
// ⚠ APPENDED, NEVER INSERTED: the category is stored BY NUMBER, so a value
// taken in the middle re-reads every existing row as a different tenant.
enum class ibSettingsCategory : int {
	Custom = 0,   // whatever script saves under a name of its own
	Form,         // how a person arranged a window — sizes, columns, what was open
	Composer,     // a report composer's user settings
	// ⭐ A LIST ON A FORM IS ITS OWN TENANT (Max, 2026-08-26). It saves settings exactly as a report
	// does — that part is NOT the variants question, where a list legitimately has none — but what it
	// saves belongs to a control on a form, not to a composer declared in the configuration. Two
	// addressing vocabularies, so two categories, and neither can collide with the other.
	List,
	// ⭐⭐ WHICH ONE COMES BACK BY ITSELF — a CATEGORY of its own rather than a column or a flag (Max,
	// 2026-08-26: *"that is not in the table, that mark lives in the NODE… you add a separate
	// category, the default setting"*). One row per object per person, holding the ID of the setting
	// to restore when the object is merely opened. A pointer is a different KIND of thing from a
	// setting, so it lives on its own shelf, and the storage still stores nothing but nodes.
	Default,
	// ⭐ THE PLATFORM ITSELF IS A TENANT. The MCP server's settings — whether it listens, and where —
	// belong to A PERSON IN A BASE, not to the installation: every developer who opens the designer
	// configures their own server, and two of them on one machine are two servers on two ports. The
	// user half of the address already says exactly that, so this is a saved setting like any other
	// rather than a file beside the executable. Its object key is the server's own well-known id:
	// what is configured here is the platform, not something in the tree.
	//
	// ⚠ APPENDED, as the rule above requires — taken in the middle it would have re-read every
	// Default row as this one.
	Mcp,
	// ⭐ THE BASE ITSELF IS A TENANT TOO: the locale its words are said in belongs to the base and every
	// client of it, so the row has NO user (ReadLocale, backend/appData.cpp). One well-known object
	// key, as the platform's above.
	Regional,
};

// ONE SAVED SETTING as a shelf shows it — the identity, the caption, and when it
// was written. The caption comes out of the NODE (ibListComposerSettings reads it),
// not out of the storage: reading what is inside a node is not the storage's business.
struct ibComposerSettingsEntry {
	ibGuid     m_id;
	ibString   m_name;
	ibDateTime m_changed;
};

#endif // __SETTINGS_SHELF_H__
