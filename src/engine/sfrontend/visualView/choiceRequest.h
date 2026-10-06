#ifndef __CHOICE_REQUEST_H__
#define __CHOICE_REQUEST_H__

// A CHOICE PUT TO THE PERSON — one of a list, through the current session's frame (ibBackendDocFrame::Request,
// ibRequestKind::Choice: a caption, and one child per item with its Id, Caption, Picture and whether it is
// Selected now; the response's Id names the item chosen). The one shape for every "pick one of these" a form on the
// server makes: a quick choice, a type among several, the object to base a new one on.

#include <vector>

#include <wx/string.h>

#include "sfrontend/sfrontend.h"
#include "backend/backend_picture.h"    // ibServerPicture — the item's picture as it is sent
#include "backend/fileSystem/types.h"   // s32

struct ibChoiceItem {
	s32             id = 0;
	wxString        caption;
	ibServerPicture icon;
	bool            selected = false;
};

// False: nothing chosen — cancelled, or nobody to ask. Only what was offered can come back: a response naming
// an Id that is not among `items` is no choice at all.
SFRONTEND_API bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, s32& chosen);

enum class ibSettingsCategory : int;   // backend/settings/settingsStorage.h — an enum cannot be declared in a signature

// ⭐⭐ THE READER'S OWN SHELF, the restoring half — which of the settings THIS person saved under `objectKey` to
// put on `composer`, put to them as a choice. Picking one IS SetUserSettingsDesc (ibRestoreComposerSettings
// ends in it), the same act as picking a variant. An empty shelf and a refused restore are SAID. The shelf's
// housekeeping — rename, delete, the mark "restore on open" — is not a choice and is not here. One road for a
// report and a list: true — a setting was put on, and what follows is the caller's (a list reads again, a
// report waits for Compose).
SFRONTEND_API bool ibChooseSavedSettings(class ibDataComposer& composer, ibSettingsCategory category,
	const class ibGuid& objectKey, const class ibMetaData* metaData);

#endif
