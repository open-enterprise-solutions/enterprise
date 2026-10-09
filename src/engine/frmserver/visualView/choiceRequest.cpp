#include "choiceRequest.h"

#include "backend/backend_mainFrame.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "protocol/protocol.h"      // ibProtocolRequestKind::Choice

namespace {

// The request a choice is put as — its caption and the items offered.
ibDataNode ChoiceRequest(const wxString& caption, const std::vector<ibChoiceItem>& items)
{
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::Choice));
	request.SetValue(wxT("Caption"), caption);
	for (const ibChoiceItem& item : items) {
		ibDataNode& node = request.AddChild(0, 0);
		node.SetValue(wxT("Id"), item.id);
		node.SetValue(wxT("Caption"), item.caption);
		if (item.icon.IsOk())
			node.SetValue(wxT("Picture"), wxString(item.icon.GetData()));
		if (item.selected)
			node.SetValue(wxT("Selected"), true);
	}
	return request;
}

} // namespace

bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, s32& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request = ChoiceRequest(caption, items);
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

bool ibRequestChoice(const wxString& caption, const std::vector<ibChoiceItem>& items, std::vector<s32>& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request = ChoiceRequest(caption, items);
	request.SetValue(wxT("Multiple"), true);
	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Only what was offered comes back, in the order it was offered.
	const ibDataValue* const ids = response.FindField(wxT("Ids"));
	if (ids == nullptr || ids->Kind() != ibDataKind::Array)
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
		node.SetValue(wxT("Id"), item.id);
		node.SetValue(wxT("Caption"), item.caption);
		if (item.checked)
			node.SetValue(wxT("Checked"), true);
		if (!item.enabled)
			node.SetValue(wxT("Enabled"), false);
		WriteMenu(node, item.items);
	}
}

// …and whether `id` is a command of it that may be picked.
bool IsMenuCommand(const std::vector<ibMenuItem>& items, s32 id)
{
	for (const ibMenuItem& item : items) {
		if (item.items.empty() ? item.id == id && item.enabled : IsMenuCommand(item.items, id))
			return true;
	}
	return false;
}

} // namespace

bool ibRequestMenu(const std::vector<ibMenuItem>& items, s32& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || items.empty())
		return false;

	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::Menu));
	WriteMenu(request, items);

	ibDataNode response;
	if (!frame->Request(request, response) || response.FindField(wxT("Id")) == nullptr)
		return false;   // dismissed

	const s32 id = response.GetValue<s32>(wxT("Id"));
	if (!IsMenuCommand(items, id))
		return false;
	chosen = id;
	return true;
}
