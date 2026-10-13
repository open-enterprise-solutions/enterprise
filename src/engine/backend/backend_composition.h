#ifndef __BACKEND_COMPOSITION_H__
#define __BACKEND_COMPOSITION_H__

// ⭐⭐ WHAT THIS SIDE RESTORES FROM A COMPOSITION. The description (core/description/compositionDescription.h)
// keeps every value PACKED — a filter's literal, a parameter's, an appearance's — and stores nothing else: no
// runtime value, no configuration, no check. The runtime restores what it runs from it, here, against the
// configuration it runs in (Max, 2026-10-12: "the schema keeps the values serialised; the runtime restores
// them from the schema").
#include "backend/backend_core.h"
#include "backend/compiler/value.h"
#include "core/description/compositionDescription.h"

// ⭐ THE TWO DOORS BETWEEN A STORE AND A RUNTIME VALUE — the only places where one becomes the other.
// Reading needs the configuration the value is read against (references and enum members are built
// by the metadata, not by the value factory); writing needs nothing, a value packs itself.
BACKEND_API ibValue    ibStoredValue(const ibDataNode& stored, const class ibMetaData* metaData);
BACKEND_API void       ibStoreValue(ibDataNode& stored, const ibValue& value);
BACKEND_API ibDataNode ibStoreValue(const ibValue& value);   // …the node itself, for a line built in one go

// ⭐ THE READ ON THIS SIDE, AGAINST THE CONFIGURATION IT BELONGS TO. The core one keeps every value packed and
// asks nothing; this one reads each of them back once, so a value this configuration cannot build (a type it no
// longer has) refuses the read here rather than the run later. No configuration — the core read alone.
class BACKEND_API ibBackendSettingsDescriptionMemory : public ibSettingsDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibSettingsDescription& settings, const class ibMetaData* metaData = nullptr);
};

class BACKEND_API ibBackendCompositionDescriptionMemory : public ibCompositionDescriptionMemory {
public:
	static bool ReadNode(const class ibDataValue& value, ibCompositionDescription& composition, const class ibMetaData* metaData = nullptr);
	static bool ReadNode(const ibDataNode& node, ibCompositionDescription& composition, const class ibMetaData* metaData = nullptr);
};

// ⭐ WHAT A PERSON READS OF A VARIANT — the synonym IN THE CONFIGURATION'S LANGUAGE, else the name. The synonym
// is written down as a translated text (`en = '…'; ru = '…'; uk = '…';`), which is a FORM of storage and not a
// caption: handed straight to a menu it put that whole line in front of the reader (2026-09-16). Every reader of
// a variant's caption asks here, so the language is decided in one place - and the two localizations stay apart:
// the buttons around it are the PLATFORM's, this text is the CONFIGURATION's and comes from the configuration.
BACKEND_API wxString ibVariantCaption(const ibVariantDescription& variant);

// ⭐ SETTINGS THAT CANNOT BE APPLIED ARE REFUSED, not silently dropped. A condition with no field,
// or a value whose type the field cannot hold, would either narrow the list by nothing or make the
// query lie — so this RAISES (ibBackendCoreException) rather than answering false.
//
// ONE CHECK, TWO FACES: the runtime gets an exception it can catch; a settings window catches the
// same one and shows the message as a warning instead of closing. Nothing is validated twice.
BACKEND_API void ibValidateSettings(const ibSettingsDescription& settings);

#endif
