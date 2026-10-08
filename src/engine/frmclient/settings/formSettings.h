#ifndef _FRMCLIENT_FORM_SETTINGS_H__
#define _FRMCLIENT_FORM_SETTINGS_H__

/////////////////////////////////////////////////////////////////////////////
// HOW A PERSON ARRANGED A FORM FOR THEMSELVES — the desktop's door (frontend/settings/formSettings.h), as its form editor
// calls it. The setting is the server's (frmserver/settings/formSettings.h): what is stored is asked of it, and an act
// is the editor's answer (ibProtocolRequestKind::FormEditor) — done there, and the editor asked again.
/////////////////////////////////////////////////////////////////////////////

#include "frmclient/frmclient.h"

class ibValueForm;

// (The desktop's RESTORE is the server's: it lays the person's setting over its form as it opens, and the client reads
//  the form as the frame says it stands.)

enum class ibFormSettingsResult {
	Ok,
	NoStorage,
	NoAddress,
	Refused,
};

// SAVE — the form as the server holds it now, written down for this person.
FRMCLIENT_API ibFormSettingsResult ibSaveFormSettings(const ibValueForm* form);

// RESET — the person's arrangement dropped: the form comes back as the author made it on the next open.
FRMCLIENT_API ibFormSettingsResult ibResetFormSettings(const ibValueForm* form);

// Is there anything saved for this form and this person?
FRMCLIENT_API bool ibHasFormSettings(const ibValueForm* form);

#endif
