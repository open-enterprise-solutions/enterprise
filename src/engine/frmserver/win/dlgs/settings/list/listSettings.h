#ifndef __LIST_SETTINGS_DLG_H__
#define __LIST_SETTINGS_DLG_H__

#include "backend/backend.h"   // BACKEND_API

class BACKEND_API ibValueModel;
class BACKEND_API ibMetaData;

// ibDialogListSettings — THE DYNAMIC LIST'S OWN settings window: the desktop's (frontend/win/dlgs/settings/list/
// listSettings.h), drawn by the client (frmclient/win/dlgs/settings/list/listSettings.h).
//
// The sequence is the desktop's ShowUserSettings, and it lives here: take a COPY of the setting in force, describe the
// fields the window needs, let the person change the copy — the client's window, asked (ibProtocolRequestKind::
// ListSettings) — and on OK set it on the model's composer and have the model read again. What crosses is a setting
// and a description of the fields, never the running object.
class ibDialogListSettings {
public:

	// ⭐⭐ THE USER'S SETTINGS OF A MODEL — the desktop's one static door. True when the setting was changed.
	static bool ShowUserSettings(ibValueModel* model, const ibMetaData* metaData);
};

#endif // __LIST_SETTINGS_DLG_H__
