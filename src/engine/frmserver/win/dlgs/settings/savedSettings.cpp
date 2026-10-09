#include "savedSettings.h"

#include "backend/backend_mainFrame.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "protocol/protocol.h"   // ibProtocolRequestKind::SavedSettings, ibProtocolSettingsAct

namespace {

// The shelf as the window shows it: the entries, newest first, and the one marked to come back on open.
ibDataNode ShelfRequest(ibDialogSavedSettings::Mode mode, ibSettingsCategory category, const ibGuid& objectKey)
{
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::SavedSettings));
	request.SetValue(wxT("Mode"), static_cast<s32>(mode));
	request.SetValue(wxT("Key"), objectKey.str());
	const ibGuid defaultId = ibGetDefaultComposerSettings(objectKey);
	if (defaultId.isValid())
		request.SetValue(wxT("Default"), defaultId.str());
	for (const ibComposerSettingsEntry& entry : ibListComposerSettings(category, objectKey)) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(wxT("Id"), entry.m_id.str());
		item.SetValue(wxT("Caption"), entry.m_name);
	}
	return request;
}

} // namespace

bool ibDialogSavedSettings::Show(ibDataComposer& composer, Mode mode, ibSettingsCategory category,
                                 const ibGuid& objectKey, const ibMetaData* metaData)
{
	if (!objectKey.isValid())
		return false;   // nothing for a shelf to be about

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// ⭐ WHAT CHANGED, not what the person pressed to leave — as the desktop's window keeps it: Close after a rename
	// still reports true, because the rename did happen.
	bool changed = false;
	ibDataNode request = ShelfRequest(mode, category, objectKey);
	for (;;) {
		ibDataNode response;
		if (!frame->Request(request, response) || response.FindField(wxT("Act")) == nullptr)
			return changed;   // closed

		// No Id — a new entry with a new identity (save), or no mark at all (default).
		const ibGuid id = response.FindField(wxT("Id")) != nullptr ? response.GetValue<ibGuid>(wxT("Id")) : wxNullGuid;
		const wxString name = response.GetValue<wxString>(wxT("Name"));

		bool done = false;
		switch (static_cast<ibProtocolSettingsAct>(response.GetValue<s32>(wxT("Act")))) {
		case ibProtocolSettingsAct::Save:
			done = ibSaveComposerSettings(category, objectKey, id, name, composer).isValid();
			break;
		case ibProtocolSettingsAct::Restore:
			done = ibRestoreComposerSettings(category, objectKey, id, composer, metaData);
			break;
		case ibProtocolSettingsAct::Default:
			done = ibSetDefaultComposerSettings(objectKey, id);
			break;
		case ibProtocolSettingsAct::Rename:
			done = ibRenameComposerSettings(category, objectKey, id, name);
			break;
		case ibProtocolSettingsAct::Remove:
			done = ibRemoveComposerSettings(category, objectKey, id);
			break;
		default:
			return changed;   // an act this server does not know — taken for closed
		}

		changed = changed || done;
		request = ShelfRequest(mode, category, objectKey);
		request.SetValue(wxT("Done"), done);
	}
}
