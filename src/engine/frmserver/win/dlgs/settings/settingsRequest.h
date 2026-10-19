#ifndef __SETTINGS_REQUEST_H__
#define __SETTINGS_REQUEST_H__

// A SETTINGS WINDOW PUT TO THE PERSON — a list's (ibDialogListSettings, ibProtocol::ibRequest::ibKind::ListSettings) and a
// report's (ibDialogComposerSettings, ComposerSettings), through the current session's frame. The window edits a COPY
// of the setting in force; on the way it asks the server what only the configuration answers (ibProtocol::ibRequest::ibListSettings::ibAct:
// the fields of a reference's targets, a value chosen), and its answer is read against the configuration — one the
// schema refuses is asked again, with why. What the window is told besides the setting — its fields, and what it
// arranges — is the door's.

#include <vector>

#include <wx/string.h>

#include "frmserver/frmserver.h"
#include "core/core.h"    // ibMetaID

class ibDataNode;
class ibMetaData;
struct ibTypeDescription;
struct ibPictureDescription;
struct ibSettingsDescription;

// ONE FIELD, DESCRIBED — what the window's field tree puts up: its name, how it reads, its column, its type, whether the
// options leave it, and its picture — the column's (GetSourcePicture) where the desktop's tree walks a source's explorer;
// none on a flat list, which the tree draws with the attribute picture.
FRMSERVER_API void ibWriteSettingsField(ibDataNode& fields, const wxString& name, const wxString& presentation, ibMetaID id,
	const ibTypeDescription& type, bool available, const ibPictureDescription& picture, const ibMetaData* metaData);

// WHICH OF THESE TYPES ARE REFERENCES, AND TO WHAT — the answer the desktop's field tree asks the configuration for
// (ibValueReferenceDataObject::ConvertToMetaIds) before it gives a field its [+]; the client has no configuration to ask.
FRMSERVER_API void ibWriteSettingsReferences(ibDataNode& references, const std::vector<ibTypeDescription>& types,
	const ibMetaData* metaData);

// THE FIELDS OF A REFERENCE'S TARGETS — a settings window's Act Expand, answered: `expanded` one field per name (its type
// the union of the targets'), `references` which of their types are references. For a door that asks no window (a
// composer's tab, ibProtocol::ibView::ibComposer::ibRequest::Expand).
FRMSERVER_API void ibWriteSettingsTargetFields(ibDataNode& expanded, ibDataNode& references,
	const std::vector<ibMetaID>& targets, const ibMetaData* metaData);

// The window, asked with `question` (its Kind, and what the door tells it) and `edited` — the setting it edits. True: OK,
// with `edited` the setting given back, read and taken by the schema. False: cancelled, or nobody to ask — `edited` as
// it was.
FRMSERVER_API bool ibRequestSettings(const ibDataNode& question, ibSettingsDescription& edited, const ibMetaData* metaData);

#endif // __SETTINGS_REQUEST_H__
