#ifndef __COMPOSER_SETTINGS_DLG_H__
#define __COMPOSER_SETTINGS_DLG_H__

#include "backend/backend.h"   // BACKEND_API

class BACKEND_API ibValueSpreadsheetModel;

// ibDialogComposerSettings — A REPORT'S settings window: the desktop's (frontend/win/dlgs/settings/composer/
// composerSettings.h), drawn by the client (frmclient/win/dlgs/settings/composer/composerSettings.h).
//
// The sequence is the desktop's ShowUserSettings, and it lives here: the composition resolves itself, a COPY of the
// setting in force is taken, the person changes the copy — the client's window, asked (ibProtocolRequestKind::
// ComposerSettings) with the composition it arranges and the fields its query offers — and on OK it is set on the
// model's composer. What crosses is a setting and descriptions, never the running object.
class ibDialogComposerSettings {
public:

	// ⭐⭐ THE USER'S SETTINGS OF A MODEL — the desktop's door. True when the setting was changed. The report is not
	// composed again here (the desktop's door composes itself): it waits for Compose, and its box says it is outdated.
	static bool ShowUserSettings(ibValueSpreadsheetModel* model);
};

#endif // __COMPOSER_SETTINGS_DLG_H__
