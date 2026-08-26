#ifndef __SETTINGS_STORAGE_H__
#define __SETTINGS_STORAGE_H__

/////////////////////////////////////////////////////////////////////////////
// ibSettingsStorage — WHERE A SAVED SETTING LIVES: one row per setting in the
// sys_settings app-table, keyed by category + object + name + user.
//
// ⭐⭐ IT STORES A NODE, AND NOTHING ELSE — that is the whole mechanism (Max,
// 2026-08-26: *"your job is just to pour a node in there; and a node may be
// generated from a value, from a schema, whatever"*). A caller hands over an
// ibDataNode; the provider writes it as bytes and the bytes go in the blob.
//
// ⭐ SO THERE IS NO VALUE DOOR HERE, deliberately. A runtime value knows how to
// become a node (ibValue::Serialize) and how to come back from one
// (ibValue::FromNode / the metadata door) — that is the VALUE's business, and
// whoever saves values does it on their own side, under a category of their own.
// A pair of value overloads here would put a second question in the storage —
// "which configuration do I read this through" — that it has no business
// answering: the caller reads the node back with whatever it stored.
//
// The same for a setting: what a composition saves is the node its own *Memory
// pair writes. This file knows neither shape and gains nothing when a new kind
// of setting appears.
//
// ⭐ THE CATEGORY IS WHAT SEPARATES THE TENANTS. Script saving under a name of
// its own, a window remembering how it was arranged, a report's user settings:
// each addresses its rows in its own vocabulary, and two of them picking the
// same object name is not a collision because the category is part of the key.
// Everything about who may use which category is decided by whoever owns the
// category, never here.
//
// OWNED BY ibApplicationData, like the job manager and the lock manager —
// created with it, reached through ibApplicationData::GetSettingsStorage()
// (nullptr pre-appData / post-appData; callers must null-check).
/////////////////////////////////////////////////////////////////////////////

#include "backend/appDataCtorToken.h"
#include "backend/compiler/value.h"

#include <vector>

#include <wx/datetime.h>   // wxDateTime — when a setting was last written (MSVC drags it in, libstdc++ does not)

class ibDataNode;

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
};

// THE ADDRESS OF ONE SETTING. Four parts, each a question of its own, because a
// single glued string would put the separator in every caller's hands — and the
// day one of them holds a name with the separator in it, two settings become
// one.
//
//   m_objectKey  — WHAT was configured. A metaobject's guid where there is one:
//                  it survives a rename, which a name does not.
//   m_settingKey — WHICH of that object's settings — an ID, minted when the
//                  setting is first saved and never changed after. ⭐ The CAPTION
//                  a person gave it lives inside the node instead (Max,
//                  2026-08-26: *"each setting will carry a unique identifier
//                  inside it, and you simply store that identifier"*), so a
//                  rename is a rewrite and nothing that points HERE is disturbed
//                  — the default pointer above all. Empty where the object has
//                  exactly one thing to say (the Default category's pointer).
//   m_userKey    — WHOSE. A user's guid; EMPTY means the setting is shared by
//                  everybody, which is also what a base with no users at all
//                  (open access) produces.
struct BACKEND_API ibSettingsKey {
	ibSettingsCategory m_category = ibSettingsCategory::Custom;
	// ⭐ GUIDS, NOT STRINGS — every part of this address IS one (Max, 2026-08-26: *"you can carry it
	// as a unique identifier; you are putting a unique identifier in there anyway"*). A metaobject's
	// guid, a setting's own minted guid, a user's guid: typed, so a caller cannot hand over a name
	// where an identity belongs, and the column below is what a guid renders as, not what it is.
	ibGuid             m_objectKey;
	ibGuid             m_settingKey;
	ibGuid             m_userKey;

	ibSettingsKey() = default;
	ibSettingsKey(ibSettingsCategory category, const ibGuid& objectKey,
	              const ibGuid& settingKey = wxNullGuid, const ibGuid& userKey = wxNullGuid)
		: m_category(category), m_objectKey(objectKey), m_settingKey(settingKey), m_userKey(userKey) {}

	// AN ADDRESS WITHOUT AN OBJECT IS NOT AN ADDRESS. The other three parts are
	// legitimately empty (one setting per object, a shared row); this one is not.
	bool IsOk() const { return m_objectKey.isValid(); }
};

// ONE ENTRY OF WHAT AN OBJECT HAS SAVED — what a menu is built from. The payload is
// deliberately absent: listing is a question about NAMES, and reading every blob to
// answer it would make opening a menu as expensive as restoring all of them.
struct BACKEND_API ibSettingsEntry {
	ibGuid     m_settingKey;   // the address INSIDE the object — an identity, not a caption
	wxDateTime m_changed;
};

class BACKEND_API ibSettingsStorage {
public:
	// Construction restricted to ibApplicationData via the token gate — the same
	// pattern as the connection pool, the lock manager and the job manager.
	explicit ibSettingsStorage(ib::AppDataCtorToken);
	~ibSettingsStorage();

	ibSettingsStorage(const ibSettingsStorage&)            = delete;
	ibSettingsStorage& operator=(const ibSettingsStorage&) = delete;

	// SAVE — the node is written through the provider and the row is upserted.
	// False when the address is incomplete or the write did not land.
	//
	// A setting saved twice is one row, not two: the upsert matches on the
	// address, so re-saving is an update whatever the driver spells it as.
	bool Save(const ibSettingsKey& key, const ibDataNode& node);

	// RESTORE — false when there is no such row, so the caller keeps whatever it
	// had. That is the answer a reader wants: "nobody saved anything here" is not
	// an error, it is the ordinary state of a base where the person has not
	// touched their settings yet.
	//
	// What the node MEANS is the caller's: a composition reads it back through its
	// *Memory pair and its own configuration, a value through ibValue::FromNode or
	// the metadata door. Both already exist; neither belongs here.
	bool Restore(const ibSettingsKey& key, ibDataNode& node) const;

	// REMOVE — clearing a setting has to reach the base too, or the next open
	// brings back what the person just cleared. True also when the row was
	// already absent: the caller asked for it to be gone, and it is.
	bool Remove(const ibSettingsKey& key);

	// WHAT THIS OBJECT HAS SAVED, newest first — the list a menu is built from.
	// Takes the address WITHOUT a name, because that is the question: everything
	// this person saved about this object.
	std::vector<ibSettingsEntry> List(ibSettingsCategory category, const ibGuid& objectKey,
	                                  const ibGuid& userKey) const;

	// (NO RENAME. The name a person gives a setting lives INSIDE the node, so
	//  renaming is writing the node again under the same address — there is
	//  nothing here for it to do, and an address that never changes is what lets
	//  the default pointer keep pointing at a renamed setting.)

private:
	// The row's own key — a deterministic hash of the four address parts, so one
	// column carries a compound address. (The DDL renderer spells PRIMARY KEY per
	// column, and sys_lock already solved this the same way: keyHash beside the
	// readable columns.)
	static wxString HashKey(const ibSettingsKey& key);
};

#endif // !__SETTINGS_STORAGE_H__
