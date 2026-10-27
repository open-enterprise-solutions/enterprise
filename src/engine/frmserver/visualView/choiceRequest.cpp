#include "choiceRequest.h"

#include "backend/backend_mainFrame.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/metadataConfiguration.h"   // the session's configuration, whose pictures these are

#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — an item's picture, by its id
#include "protocol/protocol.h"      // ibProtocol::ibRequest::ibKind::Choice

namespace {

// The request a choice is put as — its caption and the items offered.
ibDataNode ChoiceRequest(const ibString& caption, const std::vector<ibChoiceItem>& items, const ibMetaData* metaData)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Choice));
	request.SetValue(ibProtocol::ibRequest::ibChoice::Caption, caption);
	for (const ibChoiceItem& item : items) {
		ibDataNode& node = request.AddChild(0, 0);
		node.SetValue(ibProtocol::ibRequest::ibChoice::Id, item.id);
		node.SetValue(ibProtocol::ibRequest::ibChoice::Caption, item.caption);
		if (!item.icon.IsEmptyPicture())
			node.SetValue(ibProtocol::ibRequest::ibChoice::Picture, ibBackendPicture::SendPicture(item.icon, metaData));
		if (item.selected)
			node.SetValue(ibProtocol::ibRequest::ibChoice::Selected, true);
	}
	return request;
}

} // namespace

bool ibRequestChoice(const ibString& caption, const std::vector<ibChoiceItem>& items, std::int32_t& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request = ChoiceRequest(caption, items, frame->GetSession()->GetMetaData());
	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	const std::int32_t id = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibChoice::Id);
	for (const ibChoiceItem& item : items) {
		if (item.id == id) {
			chosen = id;
			return true;
		}
	}
	return false;
}

bool ibRequestChoice(const ibString& caption, const std::vector<ibChoiceItem>& items, std::vector<std::int32_t>& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request = ChoiceRequest(caption, items, frame->GetSession()->GetMetaData());
	request.SetValue(ibProtocol::ibRequest::ibChoice::Multiple, true);
	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Only what was offered comes back, in the order it was offered.
	const auto ids = response.FindField(ibProtocol::ibRequest::ibChoice::Ids);
	if (!ids || ids->Kind() != ibDataKind::Array)
		return false;
	chosen.clear();
	for (const ibChoiceItem& item : items) {
		for (const ibDataValue& id : ids->AsArray()) {
			if (id.Kind() == ibDataKind::Number && id.AsInt() == item.id) {
				chosen.push_back(item.id);
				break;
			}
		}
	}
	return !chosen.empty();
}

namespace {

// The menu's items as the request carries them — a submenu's under its item.
void WriteMenu(ibDataNode& parent, const std::vector<ibMenuItem>& items)
{
	for (const ibMenuItem& item : items) {
		ibDataNode& node = parent.AddChild(0, 0);
		node.SetValue(ibProtocol::ibRequest::ibMenu::Id, item.id);
		node.SetValue(ibProtocol::ibRequest::ibMenu::Caption, item.caption);
		if (item.checked)
			node.SetValue(ibProtocol::ibRequest::ibMenu::Checked, true);
		if (!item.enabled)
			node.SetValue(ibProtocol::ibRequest::ibMenu::Enabled, false);
		WriteMenu(node, item.items);
	}
}

// …and whether `id` is a command of it that may be picked.
bool IsMenuCommand(const std::vector<ibMenuItem>& items, std::int32_t id)
{
	for (const ibMenuItem& item : items) {
		if (item.items.empty() ? item.id == id && item.enabled : IsMenuCommand(item.items, id))
			return true;
	}
	return false;
}

} // namespace

bool ibRequestMenu(const std::vector<ibMenuItem>& items, std::int32_t& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Menu));
	WriteMenu(request, items);

	ibDataNode response;
	if (!frame->Request(request, response) || !response.FindField(ibProtocol::ibRequest::ibMenu::Id))
		return false;   // dismissed

	const std::int32_t id = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibMenu::Id);
	if (!IsMenuCommand(items, id))
		return false;
	chosen = id;
	return true;
}
