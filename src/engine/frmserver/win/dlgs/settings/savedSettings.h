#ifndef _SAVED_SETTINGS_H__
#define _SAVED_SETTINGS_H__

#include "backend/settings/settingsComposer.h"   // the shelf itself — entries, the default pointer

class ibDataComposer;
class ibMetaData;

// ibDialogSavedSettings — WHAT THIS PERSON KEPT, and what may be done to it: the desktop's window
// (frontend/win/dlgs/settings/savedSettings.h), drawn by the client (frmclient/win/dlgs/settings/savedSettings.h).
//
// ⭐ THE WINDOW STAYS OPEN THROUGH EVERY ACT BUT THE LAST, as the desktop's does. Each act the client's window does to
// the shelf is its answer (ibProtocolRequestKind::SavedSettings); it is done here, by the very calls the desktop's window
// makes, and the shelf as it now stands is asked again — Done saying how the act went — until the window closes.
class ibDialogSavedSettings {
public:

	// WHICH ACT THE WINDOW IS FOR — only the main button differs.
	enum class Mode {
		Save,      // …where to put what is in force
		Restore,   // …which one to put on
	};

	// THE DOOR. True when something changed — a setting was saved, restored, renamed, dropped, or the default mark
	// moved; the caller decides what that is worth.
	static bool Show(ibDataComposer& composer, Mode mode, ibSettingsCategory category,
	                 const ibGuid& objectKey, const ibMetaData* metaData);
};

#endif // !_SAVED_SETTINGS_H__
