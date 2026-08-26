#include "settingsComposer.h"

#include "backend/appData.h"                        // the session's user + GetSettingsStorage
#include "backend/composition/dataComposer.h"       // the settings themselves
#include "backend/compositionDescription.h"         // ibSettingsDescriptionMemory — the pair that writes them
#include "backend/serialize/dataBuilder.h"          // ibDataNode
#include "backend/guid.h"                           // a new setting's identity

#include <algorithm>

namespace {
// THE NODE'S OWN FIELD NAMES — spelled once. A setting written under one spelling
// and read under another is a setting lost.
const wxChar* const kFieldId       = wxT("id");
const wxChar* const kFieldName     = wxT("name");
const wxChar* const kChildSettings = wxT("settings");

// NOBODY — what "there is no session and none was named" resolves to. Its guid is invalid, which
// the storage reads as the shared row: a base with no users has exactly one person's settings.
const ibUserInfo s_noUser;
} // namespace

ibSettingsKey ibUserSettingsKey(ibSettingsCategory category, const ibGuid& objectKey, const ibGuid& id,
                                const ibUserInfo* user)
{
	// NAMED = TAKEN AS NAMED. A server answers for people who are not the session it happens to be
	// running on, and the caller there knows whose settings it is handling. Null = this session's,
	// which is what every desktop caller means and none of them should have to say.
	//
	// ⚠ THE WHOLE RECORD travels, not a guid pulled out of it at the call site (Max, 2026-08-26:
	// *"instead of the key, just pass the user structure"*): the caller hands over WHO, and which
	// part of a person identifies them stays this file's business.
	const ibUserInfo& info = user != nullptr ? *user
		: (appData != nullptr ? appData->GetUserInfo() : s_noUser);

	// The record keeps its guid as text (it is written to sys_user that way); here it becomes what
	// it is. A base with no users at all yields an invalid one, which the storage reads as "shared
	// by everybody" — the right answer for a base with one person.
	return ibSettingsKey(category, objectKey, id, ibGuid(info.m_strUserGuid));
}

ibGuid ibSaveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                const ibGuid& id, const wxString& name, const ibDataComposer& composer, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid())
		return wxNullGuid;

	// ⭐ THE IDENTITY IS MINTED ONCE AND IS ALSO THE ADDRESS. Saving over an entry keeps its id, so
	// the default pointer and anything else naming it go on naming the same thing — including
	// through a rename, which is this call with the same id and a different caption.
	const ibGuid entryId = id.isValid() ? id : ibGuid(wxNewUniqueGuid);

	ibDataNode node;
	node.SetValue(kFieldId,   entryId);
	node.SetValue(kFieldName, name);

	// WHAT COMPOSES, written by the settings' own pair into a child of its own. Nothing here knows
	// what a setting consists of, and nothing has to be told when it grows a part.
	if (!ibSettingsDescriptionMemory::WriteNode(node.Child(kChildSettings), composer.GetCurrentSettingsDesc()))
		return wxNullGuid;

	if (!storage->Save(ibUserSettingsKey(category, objectKey, entryId, user), node))
		return wxNullGuid;

	return entryId;
}

bool ibRestoreComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                               const ibGuid& id, ibDataComposer& composer, const ibMetaData* metaData, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid() || !id.isValid())
		return false;

	ibDataNode node;
	if (!storage->Restore(ibUserSettingsKey(category, objectKey, id, user), node))
		return false;

	const ibDataNode* settingsNode = node.FindChild(kChildSettings);
	if (settingsNode == nullptr)
		return false;

	ibSettingsDescription settings;
	if (!ibSettingsDescriptionMemory::ReadNode(*settingsNode, settings, metaData))
		return false;

	// …AND THAT IS THE WHOLE ACT — the one door there is, the same one the variant picker and the
	// settings window use.
	composer.SetUserSettingsDesc(settings);
	return true;
}

std::vector<ibComposerSettingsEntry> ibListComposerSettings(ibSettingsCategory category, const ibGuid& objectKey, const ibUserInfo* user)
{
	std::vector<ibComposerSettingsEntry> entries;

	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid())
		return entries;

	const ibSettingsKey address = ibUserSettingsKey(category, objectKey, wxNullGuid, user);
	for (const ibSettingsEntry& row : storage->List(category, objectKey, address.m_userKey)) {
		ibComposerSettingsEntry entry;
		entry.m_id      = row.m_settingKey;
		entry.m_changed = row.m_changed;

		// THE CAPTION IS IN THE NODE, so listing reads it. A handful of entries per object, each a
		// short blob — and the alternative is a second copy of every name in a column that can
		// disagree with the node it was copied from.
		ibDataNode node;
		if (storage->Restore(ibUserSettingsKey(category, objectKey, row.m_settingKey, user), node))
			entry.m_name = node.GetValue<wxString>(kFieldName);

		// A NAMELESS ENTRY IS STILL PICKABLE — an id is not a caption a person can read, so it says
		// what it honestly is rather than showing a blank line nobody dares click.
		if (entry.m_name.IsEmpty())
			entry.m_name = _("Settings without a name");

		entries.push_back(entry);
	}
	return entries;
}

bool ibRenameComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                              const ibGuid& id, const wxString& newName, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid() || !id.isValid() || newName.IsEmpty())
		return false;

	const ibSettingsKey key = ibUserSettingsKey(category, objectKey, id, user);

	// THE NODE COMES BACK AND GOES OUT AGAIN with one field different. Its settings are never read
	// as settings on this road — nothing is interpreted, so nothing can be lost in the interpreting,
	// and a caption that a future version writes beside them travels through untouched.
	ibDataNode node;
	if (!storage->Restore(key, node))
		return false;

	ibDataNode renamed;
	renamed.SetValue(kFieldId,   id);
	renamed.SetValue(kFieldName, newName);
	if (const ibDataNode* settingsNode = node.FindChild(kChildSettings))
		renamed.Child(kChildSettings) = *settingsNode;
	else
		return false;   // an entry with no settings inside is not one this can rewrite

	return storage->Save(key, renamed);
}

bool ibRemoveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey, const ibGuid& id, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid() || !id.isValid())
		return false;

	// ⚠ THE POINTER GOES WITH IT when it pointed here. A default naming an entry that no longer
	// exists restores nothing on open — silently, which reads as "the mark stopped working".
	if (ibGetDefaultComposerSettings(objectKey, user) == id)
		ibSetDefaultComposerSettings(objectKey, wxNullGuid, user);

	return storage->Remove(ibUserSettingsKey(category, objectKey, id, user));
}

// ---------------------------------------------------------------------------
// ⭐⭐ THE DEFAULT — ITS OWN CATEGORY, holding an id and nothing else (Max, 2026-08-26). Not a
// column and not a flag on the row: "which setting comes back on open" is one fact about an OBJECT,
// so it is one row about that object, and the question has one answer by construction rather than
// by every writer remembering to unset the previous one.
// ---------------------------------------------------------------------------

bool ibSetDefaultComposerSettings(const ibGuid& objectKey, const ibGuid& id, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid())
		return false;

	// ⚠ THE POINTER IS ADDRESSED BY THE OBJECT, with NO setting name — that is what makes it one per
	// object per person. The category it lives in is the pointer's own, so a setting and a pointer
	// can never collide however they are named.
	const ibSettingsKey key = ibUserSettingsKey(ibSettingsCategory::Default, objectKey, wxNullGuid, user);

	// EMPTY = "open it the way the author left it". Dropping the row rather than writing an empty
	// one keeps "nothing is marked" as the ABSENCE of a statement instead of a statement of nothing.
	if (!id.isValid())
		return storage->Remove(key);

	ibDataNode node;
	node.SetValue(kFieldId, id);
	return storage->Save(key, node);
}

ibGuid ibGetDefaultComposerSettings(const ibGuid& objectKey, const ibUserInfo* user)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr || !objectKey.isValid())
		return wxNullGuid;

	ibDataNode node;
	if (!storage->Restore(ibUserSettingsKey(ibSettingsCategory::Default, objectKey, wxNullGuid, user), node))
		return wxNullGuid;

	return node.GetValue<ibGuid>(kFieldId);
}

ibDefaultSettingsOutcome ibRestoreDefaultComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                                          ibDataComposer& composer, const ibMetaData* metaData, const ibUserInfo* user)
{
	const ibGuid id = ibGetDefaultComposerSettings(objectKey, user);
	if (!id.isValid())
		return ibDefaultSettingsOutcome::None;   // nothing marked — the ordinary state, and not a failure

	if (ibRestoreComposerSettings(category, objectKey, id, composer, metaData, user))
		return ibDefaultSettingsOutcome::Restored;

	// A MARK WITH NOTHING BEHIND IT. The row was dropped by another session, or its settings name a
	// type this configuration no longer has — either way the person asked for something on open and
	// did not get it, so the caller says so instead of composing on something else in silence.
	ibJournalWarning(wxT("settings"), wxT("default settings missing: category %d, object %s, id %s"),
		static_cast<int>(category), objectKey.str(), id.str());
	return ibDefaultSettingsOutcome::Missing;
}
