#ifndef _FRMCLIENT_BACKEND_SETTINGS_COMPOSER_H__
#define _FRMCLIENT_BACKEND_SETTINGS_COMPOSER_H__

#include <vector>

#include <wx/string.h>

#include "frmclient/frmclient.h"
#include "core/guid.h"

class ibDataComposer;
class ibMetaData;

// A PERSON'S SHELF OF SETTINGS — the engine's (backend/settings/settingsComposer.h), as the client's window of it
// (ibDialogSavedSettings) calls it. The shelf is the server's: what is listed is the question the server waits on
// (ibProtocolRequestKind::SavedSettings), and an act is its answer — done there, and the shelf asked again with how
// it went. Whose shelf it is, the server knows: the category and the key are not read here.

// What the shelf is kept under — the engine's (backend/settings/settingsStorage.h).
enum class ibSettingsCategory : int {
	Custom = 0,
	Form,
	Composer,
	List,
	Default,
	Mcp,
};

struct FRMCLIENT_API ibComposerSettingsEntry {
	ibGuid   m_id;
	wxString m_name;
};

FRMCLIENT_API ibGuid ibSaveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                           const ibGuid& id, const wxString& name,
                                           const ibDataComposer& composer);
FRMCLIENT_API bool ibRestoreComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                            const ibGuid& id, ibDataComposer& composer,
                                            const ibMetaData* metaData);
FRMCLIENT_API std::vector<ibComposerSettingsEntry> ibListComposerSettings(ibSettingsCategory category,
                                                                         const ibGuid& objectKey);
FRMCLIENT_API bool ibRenameComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                           const ibGuid& id, const wxString& newName);
FRMCLIENT_API bool ibRemoveComposerSettings(ibSettingsCategory category, const ibGuid& objectKey,
                                           const ibGuid& id);
FRMCLIENT_API bool   ibSetDefaultComposerSettings(const ibGuid& objectKey, const ibGuid& id);
FRMCLIENT_API ibGuid ibGetDefaultComposerSettings(const ibGuid& objectKey);

#endif
