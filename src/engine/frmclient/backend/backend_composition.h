#ifndef __BACKEND_COMPOSITION_H__
#define __BACKEND_COMPOSITION_H__

// ⭐⭐ WHAT THIS SIDE RESTORES FROM A COMPOSITION. The description (core/description/compositionDescription.h)
// keeps every value PACKED — a filter's literal, a parameter's, an appearance's — and stores nothing else: no
// runtime value, no configuration, no check. The client restores what it shows from it, here (Max, 2026-10-12:
// "the schema keeps the values serialised; the runtime restores them from the schema").
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/compiler/value.h"
#include "core/description/compositionDescription.h"

// ⭐ THE TWO DOORS BETWEEN A STORE AND A RUNTIME VALUE — the only places where one becomes the other.
// ON THE CLIENT there is no configuration to hand in: a primitive is read as one, any other value is kept as
// the server wrote it (ibValue::FromNode) and given back so. Writing needs nothing, a value packs itself.
FRMCLIENT_API ibValue    ibStoredValue(const ibDataNode& stored, const class ibMetaData* metaData);
FRMCLIENT_API void       ibStoreValue(ibDataNode& stored, const ibValue& value);
FRMCLIENT_API ibDataNode ibStoreValue(const ibValue& value);   // …the node itself, for a line built in one go

// ⭐ WHAT A PERSON READS OF A VARIANT — the synonym IN THE CONFIGURATION'S LANGUAGE, else the name. The synonym
// is written down as a translated text (`en = '…'; ru = '…'; uk = '…';`), which is a FORM of storage and not a
// caption: handed straight to a menu it put that whole line in front of the reader (2026-09-16).
FRMCLIENT_API wxString ibVariantCaption(const ibVariantDescription& variant);

// ⭐ SETTINGS THAT CANNOT BE APPLIED ARE REFUSED, not silently dropped — RAISED (ibBackendCoreException), so a
// settings window catches it and shows the message as a warning instead of closing.
FRMCLIENT_API void ibValidateSettings(const ibSettingsDescription& settings);

#endif
