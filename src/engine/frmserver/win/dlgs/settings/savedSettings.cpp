#include "savedSettings.h"

#include "backend/backend_mainFrame.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::SavedSettings, ibProtocol::ibRequest::ibSavedSettings::ibAct

namespace {

// The shelf as the window shows it: the entries, newest first, and the one marked to come back on open.
ibDataNode ShelfRequest(ibDialogSavedSettings::Mode mode, ibSettingsCategory category, const ibGuid& objectKey)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::SavedSettings));
	request.SetValue(ibProtocol::ibRequest::ibSavedSettings::Mode, static_cast<std::int32_t>(mode == ibDialogSavedSettings::Mode::Save
		? ibProtocol::ibRequest::ibSavedSettings::ibMode::Save : ibProtocol::ibRequest::ibSavedSettings::ibMode::Restore));
	request.SetValue(ibProtocol::ibRequest::ibSavedSettings::Key, objectKey.str());
	const ibGuid defaultId = ibGetDefaultComposerSettings(objectKey);
	if (defaultId.isValid())
		request.SetValue(ibProtocol::ibRequest::ibSavedSettings::Default, defaultId.str());
	for (const ibComposerSettingsEntry& entry : ibListComposerSettings(category, objectKey)) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibSavedSettings::Id, entry.m_id.str());
		item.SetValue(ibProtocol::ibRequest::ibSavedSettings::Caption, entry.m_name);
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
		if (!frame->Request(request, response) || !response.FindField(ibProtocol::ibRequest::ibSavedSettings::Act))
			return changed;   // closed

		// No Id — a new entry with a new identity (save), or no mark at all (default).
		const ibGuid id = response.Has(ibProtocol::ibRequest::ibSavedSettings::Id) ? response.GetValue<ibGuid>(ibProtocol::ibRequest::ibSavedSettings::Id) : wxNullGuid;
		const ibString name = response.GetValue<ibString>(ibProtocol::ibRequest::ibSavedSettings::Name);

		bool done = false;
		switch (static_cast<ibProtocol::ibRequest::ibSavedSettings::ibAct>(response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibSavedSettings::Act))) {
		case ibProtocol::ibRequest::ibSavedSettings::ibAct::Save:
			done = ibSaveComposerSettings(category, objectKey, id, name, composer).isValid();
			break;
		case ibProtocol::ibRequest::ibSavedSettings::ibAct::Restore:
			done = ibRestoreComposerSettings(category, objectKey, id, composer, metaData);
			break;
		case ibProtocol::ibRequest::ibSavedSettings::ibAct::Default:
			done = ibSetDefaultComposerSettings(objectKey, id);
			break;
		case ibProtocol::ibRequest::ibSavedSettings::ibAct::Rename:
			done = ibRenameComposerSettings(category, objectKey, id, name);
			break;
		case ibProtocol::ibRequest::ibSavedSettings::ibAct::Remove:
			done = ibRemoveComposerSettings(category, objectKey, id);
			break;
		default:
			return changed;   // an act this server does not know — taken for closed
		}

		changed = changed || done;
		request = ShelfRequest(mode, category, objectKey);
		request.SetValue(ibProtocol::ibRequest::ibSavedSettings::Done, done);
	}
}
