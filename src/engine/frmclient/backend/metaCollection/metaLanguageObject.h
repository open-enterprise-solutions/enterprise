#ifndef _FRMCLIENT_BACKEND_META_LANGUAGE_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_LANGUAGE_OBJECT_H__

#include "frmclient/backend/metaCollection/metaObject.h"

// A LANGUAGE OF THE CONFIGURATION — the engine's ibValueMetaObjectLanguage: the server's; the client lists none
// (ibMetaData), and a translated text is then edited in the person's own language.
class ibValueMetaObjectLanguage : public ibValueMetaObject {
public:
	wxString GetLangCode() const { return wxString(); }
};

constexpr ibClassID g_metaLanguageCLSID = metadata_to_clsid("MD_LANG");

#endif
