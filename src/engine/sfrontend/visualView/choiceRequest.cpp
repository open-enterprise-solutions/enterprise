#include "choiceRequest.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_picture.h"   // ibBackendPicture::CreateBase64Image — a picture as a request carries it
#include "backend/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/settings/settingsComposer.h"   // the reader's shelf — listed, and one entry of it restored

#include "sfrontend/client/clientRequest.h"      // ibRequestKind::Choice

bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, s32& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibRequestKind::Choice));
	request.SetValue(wxT("Caption"), caption);
	for (const ibChoiceItem& item : items) {
		ibDataNode& node = request.AddChild(0, 0);
		node.SetValue(wxT("Id"), item.id);
		node.SetValue(wxT("Caption"), item.caption);
		if (item.icon.IsOk())
			node.SetValue(wxT("Picture"), ibBackendPicture::CreateBase64Image(wxBitmap(item.icon).ConvertToImage()));
		if (item.selected)
			node.SetValue(wxT("Selected"), true);
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	const s32 id = response.GetValue<s32>(wxT("Id"));
	for (const ibChoiceItem& item : items) {
		if (item.id == id) {
			chosen = id;
			return true;
		}
	}
	return false;
}

bool ibChooseSavedSettings(ibDataComposer& composer, ibSettingsCategory category, const ibGuid& objectKey,
	const ibMetaData* metaData)
{
	if (!objectKey.isValid())
		return false;   // nothing bound — nothing for a shelf to be about

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();

	// NEWEST FIRST, as the shelf keeps them.
	const std::vector<ibComposerSettingsEntry> entries = ibListComposerSettings(category, objectKey);
	if (entries.empty()) {
		// 🛑 A COMMAND THAT ANSWERS NOTHING READS AS ONE THAT DOES NOT WORK — an empty shelf is an answer.
		if (frame != nullptr)
			frame->ShowModalMessage(_("No settings have been saved here yet."), _("Restore settings"),
				wxOK | wxICON_INFORMATION);
		return false;
	}

	std::vector<ibChoiceItem> items;
	for (size_t i = 0; i < entries.size(); ++i) {
		ibChoiceItem item;
		item.id = static_cast<s32>(i);
		item.caption = entries[i].m_name;
		items.push_back(item);
	}

	s32 chosen = 0;
	if (!ibRequestChoice(_("Restore settings"), items, chosen))
		return false;   // closed without choosing — nothing changes

	const ibComposerSettingsEntry& entry = entries[static_cast<size_t>(chosen)];
	if (!ibRestoreComposerSettings(category, objectKey, entry.m_id, composer, metaData)) {
		// A REFUSAL IS SPOKEN. The row may hold a value whose type this configuration no longer has —
		// the read says so and the row stays; the person is told rather than left picking an entry that
		// does nothing.
		if (frame != nullptr)
			frame->ShowModalMessage(wxString::Format(_("The settings named \"%s\" could not be restored."), entry.m_name),
				_("Saved settings"), wxOK | wxICON_ERROR);
		return false;
	}
	return true;
}
