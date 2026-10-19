#include "settingsComposer.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

namespace {

// AN ACT ON THE SHELF — the answer to the question pending: the server does it and asks again, saying how it went.
bool ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct act, const ibGuid& id, const wxString& name)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode response;
	response.SetValue(ibProtocol::ibRequest::ibSavedSettings::Act, static_cast<long long>(act));
	if (id.isValid())
		response.SetValue(ibProtocol::ibRequest::ibSavedSettings::Id, id.str());
	if (!name.IsEmpty())
		response.SetValue(ibProtocol::ibRequest::ibSavedSettings::Name, name);
	return frame->Respond(response) && frame->GetRequest().GetBool(ibProtocol::ibRequest::ibSavedSettings::Done);
}

} // namespace

ibGuid ibSaveComposerSettings(ibSettingsCategory WXUNUSED(category), const ibGuid& WXUNUSED(objectKey),
                              const ibGuid& id, const wxString& name, const ibDataComposer& WXUNUSED(composer))
{
	if (!ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct::Save, id, name))
		return wxNullGuid;
	// The entry saved into: the one named, or — a new one — the newest, which the shelf lists first.
	if (id.isValid())
		return id;
	const std::vector<ibComposerSettingsEntry> entries = ibListComposerSettings(ibSettingsCategory::Custom, wxNullGuid);
	return !entries.empty() ? entries.front().m_id : wxNullGuid;
}

bool ibRestoreComposerSettings(ibSettingsCategory WXUNUSED(category), const ibGuid& WXUNUSED(objectKey),
                               const ibGuid& id, ibDataComposer& WXUNUSED(composer), const ibMetaData* WXUNUSED(metaData))
{
	return ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct::Restore, id, wxString());
}

std::vector<ibComposerSettingsEntry> ibListComposerSettings(ibSettingsCategory WXUNUSED(category),
                                                            const ibGuid& WXUNUSED(objectKey))
{
	std::vector<ibComposerSettingsEntry> entries;
	if (ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame()) {
		for (const ibProtocolNode& item : frame->GetRequest().Children())
			entries.push_back(ibComposerSettingsEntry{ ibGuid(item.GetString(ibProtocol::ibRequest::ibSavedSettings::Id)),
				item.GetString(ibProtocol::ibRequest::ibSavedSettings::Caption) });
	}
	return entries;
}

bool ibRenameComposerSettings(ibSettingsCategory WXUNUSED(category), const ibGuid& WXUNUSED(objectKey),
                              const ibGuid& id, const wxString& newName)
{
	return ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct::Rename, id, newName);
}

bool ibRemoveComposerSettings(ibSettingsCategory WXUNUSED(category), const ibGuid& WXUNUSED(objectKey),
                              const ibGuid& id)
{
	return ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct::Remove, id, wxString());
}

bool ibSetDefaultComposerSettings(const ibGuid& WXUNUSED(objectKey), const ibGuid& id)
{
	return ActOnShelf(ibProtocol::ibRequest::ibSavedSettings::ibAct::Default, id, wxString());
}

ibGuid ibGetDefaultComposerSettings(const ibGuid& WXUNUSED(objectKey))
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr ? ibGuid(frame->GetRequest().GetString(ibProtocol::ibRequest::ibSavedSettings::Default)) : wxNullGuid;
}
