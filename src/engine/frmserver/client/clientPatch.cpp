#include "clientPatch.h"

#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "core/serialize/dataBuilder.h"

namespace {

using ibEntries = std::vector<std::pair<wxString, ibDataValue>>;

bool Diff(const ibDataNode& from, const ibDataNode& to, ibDataNode& patch);

const ibDataValue* FindEntry(const ibEntries& entries, const wxString& name)
{
	for (const auto& entry : entries) {
		if (entry.first == name)
			return &entry.second;
	}
	return nullptr;
}

// One area of entries — the fields, or the properties (a named sub-node is one) — each kept in its own area.
bool DiffEntries(const ibEntries& from, const ibEntries& to, bool properties, ibDataNode& patch,
	std::vector<ibDataValue>& removed)
{
	for (const auto& entry : to) {
		const ibDataValue* const was = FindEntry(from, entry.first);
		if (was != nullptr && *was == entry.second)
			continue;

		ibDataValue value = entry.second;
		if (was != nullptr && was->Kind() == ibDataKind::Child && entry.second.Kind() == ibDataKind::Child
			&& was->AsChild() && entry.second.AsChild()) {
			// ⚠ ANOTHER NODE UNDER THE NAME — the old one goes, this one comes whole, as a child under the same id does
			// (DiffChildren): patched, it kept the old one's type — a tab's text view, patched from a sheet's, was drawn
			// as a sheet.
			if (was->AsChild()->GetClsid() != entry.second.AsChild()->GetClsid()) {
				removed.push_back(ibDataValue::String(entry.first));
			}
			else {
				auto sub = std::make_shared<ibDataNode>();
				if (!Diff(*was->AsChild(), *entry.second.AsChild(), *sub))
					return false;
				value = ibDataValue::Child(sub);
			}
		}
		if (properties)
			patch.SetProperty(entry.first, value);
		else
			patch.AddField(entry.first, value);
	}
	for (const auto& entry : from) {
		if (FindEntry(to, entry.first) == nullptr)
			removed.push_back(ibDataValue::String(entry.first));
	}
	return true;
}

// Children named by their ids — every one has an id, and no two the same.
bool Keyed(const std::vector<ibDataNode>& children)
{
	std::set<ibMetaID> ids;
	for (const ibDataNode& child : children) {
		if (child.GetMetaId() == 0 || !ids.insert(child.GetMetaId()).second)
			return false;
	}
	return true;
}

bool DiffChildren(const std::vector<ibDataNode>& from, const std::vector<ibDataNode>& to, ibDataNode& patch)
{
	if (from == to)
		return true;

	if (!Keyed(from) || !Keyed(to)) {
		patch.AddField(wxT("NodeChildrenWhole"), ibDataValue::Bool(true));
		for (const ibDataNode& child : to)
			patch.Children().push_back(child);
		return true;
	}

	std::map<ibMetaID, const ibDataNode*> was;
	for (const ibDataNode& child : from)
		was[child.GetMetaId()] = &child;
	std::set<ibMetaID> now;
	for (const ibDataNode& child : to)
		now.insert(child.GetMetaId());

	std::vector<ibDataValue> removedIds;
	for (const ibDataNode& child : from) {
		if (now.count(child.GetMetaId()) == 0)
			removedIds.push_back(ibDataValue::Int(child.GetMetaId()));
	}

	bool inserted = false;
	for (const ibDataNode& child : to) {
		const auto found = was.find(child.GetMetaId());
		if (found == was.end() || found->second->GetClsid() != child.GetClsid()) {
			// New — or another node under the same id: the old one goes, this one comes whole.
			if (found != was.end())
				removedIds.push_back(ibDataValue::Int(child.GetMetaId()));
			patch.Children().push_back(child);
			inserted = true;
			continue;
		}
		if (*found->second == child)
			continue;
		ibDataNode& sub = patch.AddChild(child.GetClsid(), child.GetMetaId());
		if (!Diff(*found->second, child, sub))
			return false;
	}

	if (!removedIds.empty())
		patch.AddField(wxT("NodeRemovedIds"), ibDataValue::Array(removedIds));

	std::vector<ibDataValue> order, previous;
	for (const ibDataNode& child : to)
		order.push_back(ibDataValue::Int(child.GetMetaId()));
	for (const ibDataNode& child : from) {
		if (now.count(child.GetMetaId()) != 0)
			previous.push_back(ibDataValue::Int(child.GetMetaId()));
	}
	if (inserted || order != previous)
		patch.AddField(wxT("NodeOrder"), ibDataValue::Array(order));
	return true;
}

bool Diff(const ibDataNode& from, const ibDataNode& to, ibDataNode& patch)
{
	if (from.RawData().GetDataLen() != to.RawData().GetDataLen()
		|| (to.RawData().GetDataLen() != 0
			&& std::memcmp(from.RawData().GetData(), to.RawData().GetData(), to.RawData().GetDataLen()) != 0))
		return false;

	patch.SetClsid(to.GetClsid());
	patch.SetMetaId(to.GetMetaId());

	std::vector<ibDataValue> removed;
	if (!DiffEntries(from.Fields(), to.Fields(), false, patch, removed)
		|| !DiffEntries(from.Properties(), to.Properties(), true, patch, removed))
		return false;
	if (!removed.empty())
		patch.AddField(wxT("NodeRemoved"), ibDataValue::Array(removed));

	return DiffChildren(from.Children(), to.Children(), patch);
}

} // namespace

bool ibClientFramePatch(const ibDataNode& from, const ibDataNode& to, ibDataNode& patch)
{
	patch = ibDataNode();
	// A patch that says no less than the frame is not worth saying — two different forms' views, matched by control
	// ids that only happen to be the same, came out heavier than the frame (2026-10-06).
	return Diff(from, to, patch) && patch.Weight() < to.Weight();
}
