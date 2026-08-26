#ifndef __SETTINGS_COMPOSER_H__
#define __SETTINGS_COMPOSER_H__

/////////////////////////////////////////////////////////////////////////////
// THE SEAM between a composer's settings and where saved settings live — and
// it is a seam rather than a member on either side, because neither side is
// the right owner: the storage stores NODES and knows no setting's shape, and
// a composer keeps settings and knows nothing about a base.
//
// ⭐⭐ A LIST AND A REPORT GO THROUGH THE SAME CALLS. They differ only in their
// ADDRESS — a report's settings belong to the composer the configuration
// declares, a list's to the control on the form (Max, 2026-08-26: *"a list can
// have saved settings too; on a form, on a list, that is a separate
// category"*). So the address is an argument, and there is one mechanism.
//
// ⭐⭐ WHAT A SAVED SETTING IS, AS A NODE:
//
//     <row>  id = "<guid>"          ← the identity, minted once, never changed
//            name = "Sales by week" ← the caption a person typed; a RENAME rewrites this
//            settings { … }         ← the settings themselves, their own pair's work
//
// The ID is the row's ADDRESS as well, so nothing that points at a setting is
// disturbed when it is renamed — the default pointer above all (Max: *"each
// setting will carry a unique identifier inside it, and you simply store that
// identifier"*).
//
// ⚠ WHAT IS SAVED IS WHAT COMPOSES, not the reader's section alone: a person
// who saves without having changed anything means "this, the way it is right
// now", and on a fresh report that is the author's zeroth variant.
/////////////////////////////////////////////////////////////////////////////

#include "backend/settings/settingsStorage.h"
#include "backend/userInfo.h"   // ibUserInfo — WHOSE settings, handed over whole

class ibDataComposer;
class ibMetaData;

// (⛔ NO "WHERE ARE THIS CONTROL'S SETTINGS" HELPER HERE, and there is no file below this one to put
//  it in either. Working the address out means asking a SOURCE what metaobject it reads, or a
//  configuration to turn a binding's leaf into a guid — and both drag metadata into a file whose
//  whole point is that it has none: settings go in as nodes and come back as nodes (Max, 2026-08-26:
//  *"we have no suitable file for it, and it would be a metadata leak"*).
//
//  So the ADDRESS is the caller's, computed where the metadata already is — a control knows its own
//  binding, its own form and its own configuration. `metaData` below stays an opaque pointer that is
//  handed straight to the settings' own pair, which is a different thing from reaching into it.)

// ONE SAVED SETTING as a shelf shows it — the identity, the caption, and when it
// was written. The caption comes out of the NODE, which is why this lives here
// and not on the storage: reading what is inside a node is not the storage's
// business.
struct BACKEND_API ibComposerSettingsEntry {
	ibGuid     m_id;
	wxString   m_name;
	wxDateTime m_changed;
};

// THE ADDRESS. `userKey` empty = THE CURRENT SESSION'S user, which is what every
// desktop caller means and none of them should have to say.
//
// ⭐ AND IT IS AN ARGUMENT, for the server (Max, 2026-08-26: *"a user parameter
// would not hurt — to pass that user as an argument when we have a server"*).
// There, one process serves many people and the work is not always done on the
// asking session: a job restoring somebody's settings, an admin reading them,
// a web request answered off a pool thread. Reaching for "the current session"
// inside would be the same class of mistake as reaching for the active
// configuration — the caller knows whose, and says so.
//
// A base with no users at all yields an invalid guid, which the storage reads as
// "shared by everybody" — right for one person.
BACKEND_API ibSettingsKey ibUserSettingsKey(ibSettingsCategory category, const ibGuid& objectKey,
                                            const ibGuid& id = wxNullGuid, const ibUserInfo* user = nullptr);

// SAVE what the composer is composing on. `id` empty = a NEW entry and a new
// identity is minted; a given id REWRITES that entry, which is what both "save
// over this one" and "rename" are. Returns the id, empty on refusal.
BACKEND_API ibGuid ibSaveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                          const ibGuid& id, const wxString& name,
                                          const ibDataComposer& composer, const ibUserInfo* user = nullptr);

// RESTORE — one call at the end: SetUserSettingsDesc, the same act as picking a
// variant or pressing OK in the settings window. False when there is no such
// row or the node cannot be read as settings.
//
// `metaData` is the configuration the SETTING's own values are read through (a
// filter's right-hand side may be a reference or an enum member, and those live
// only in a configuration's registry). It belongs to the caller — several are
// open at once — and the pair takes it as an argument for exactly this reason.
BACKEND_API bool ibRestoreComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                           const ibGuid& id, ibDataComposer& composer,
                                           const ibMetaData* metaData, const ibUserInfo* user = nullptr);

// WHAT THIS PERSON HAS SAVED ABOUT THIS OBJECT, newest first. Reads each node
// for its caption — the shelf holds a handful of entries, and the alternative
// is a second copy of every name in a column that could disagree with the node.
BACKEND_API std::vector<ibComposerSettingsEntry> ibListComposerSettings(ibSettingsCategory category,
                                                                        const ibGuid& objectKey,
                                                                        const ibUserInfo* user = nullptr);

// RENAME — the caption lives in the node, so this reads the node, replaces the
// caption and writes it back. The SETTINGS inside are carried over untouched and
// never pass through a composer: renaming an entry is not restoring it, and a
// person renaming the entry they are not currently using must not have it
// applied to them as a side effect.
BACKEND_API bool ibRenameComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                          const ibGuid& id, const wxString& newName,
                                          const ibUserInfo* user = nullptr);

// Drop one entry. Clears the default pointer as well when it pointed here — a
// pointer to a setting that is gone would silently restore nothing on open.
BACKEND_API bool ibRemoveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                          const ibGuid& id, const ibUserInfo* user = nullptr);

// ⭐ THE ONE RESTORED ON OPEN. `id` empty CLEARS the mark — which is how a person
// says "open it the way the author left it". One row per object per person, in
// the Default category, holding the id and nothing else.
//
// ⚠ NO CATEGORY HERE, and that is the point of the address: the pointer is about
// the OBJECT, and an object belongs to one category by its nature — a composer
// is a composer, a list control is a list control. Naming the category again
// would be a second statement of a fact the object key already carries.
BACKEND_API bool   ibSetDefaultComposerSettings(const ibGuid& objectKey, const ibGuid& id,
                                                const ibUserInfo* user = nullptr);
BACKEND_API ibGuid ibGetDefaultComposerSettings(const ibGuid& objectKey, const ibUserInfo* user = nullptr);

// …and the act itself, for whoever OPENS the object.
//
// ⚠ THREE OUTCOMES, NOT TWO, and the third is why: a mark pointing at a setting
// that cannot be read is a FAULT a person must hear about (Max, 2026-08-26:
// *"and if it cannot find that setting afterwards, it complains"*), while
// "nobody marked anything" is the ordinary state of every object nobody has
// configured. Told apart here so the caller can say the one and stay silent
// about the other; the backend does not speak to people itself.
enum class ibDefaultSettingsOutcome {
	None = 0,   // nothing marked — compose on what you already had
	Restored,   // the marked setting is now in force
	Missing,    // a mark exists and its setting does not — SAY SO
};

BACKEND_API ibDefaultSettingsOutcome ibRestoreDefaultComposerSettings(ibSettingsCategory category,
                                                                      const ibGuid& objectKey,
                                                                      ibDataComposer& composer,
                                                                      const ibMetaData* metaData,
                                                                      const ibUserInfo* user = nullptr);

#endif // !__SETTINGS_COMPOSER_H__
