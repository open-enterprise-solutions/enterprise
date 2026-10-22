#include "protocolNode.h"
#include "protocolValue.h"
#include "protocol/protocol.h"   // ibProtocol::ibNode

#include <algorithm>
#include <map>
#include <set>

namespace {

using ibKind = ibProtocolValue::ibKind;

// The words a node is written with on the wire, and the patch's own — what the patch says of the node and its
// children, never an entry of it.
bool IsStructural(const ibString& name)
{
	return name == ibProtocol::ibNode::Removed || name == ibProtocol::ibNode::RemovedIds
		|| name == ibProtocol::ibNode::Order || name == ibProtocol::ibNode::ChildrenWhole
		|| name == ibProtocol::ibNode::Children || name == ibProtocol::ibNode::Type || name == ibProtocol::ibNode::Id;
}

// A child's id — 0 for one that has none.
long long IdOf(const ibProtocolValue& node)
{
	const ibProtocolBody* const entries = node.GetNode();
	const ibProtocolValue* const id = entries != nullptr ? entries->Find(ibProtocol::ibNode::Id) : nullptr;
	return id != nullptr && id->GetKind() == ibKind::Int ? id->AsInt() : 0;
}

// The list under the name, when the patch has one.
const ibProtocolBody* ListIn(const ibProtocolBody& patch, const char* name)
{
	const ibProtocolValue* const list = patch.Find(name);
	return list != nullptr ? list->GetList() : nullptr;
}

void ApplyNode(ibProtocolBody& copy, const ibProtocolBody& patch)
{
	// The entries gone, first; then each the patch carries — merged when both are nodes.
	if (const ibProtocolBody* const gone = ListIn(patch, ibProtocol::ibNode::Removed)) {
		for (const ibProtocolEntry& name : gone->m_entries) {
			if (name.m_value.GetKind() == ibKind::Text)
				copy.Remove(name.m_value.AsText());
		}
	}
	for (const ibProtocolEntry& entry : patch.m_entries) {
		if (IsStructural(entry.m_name))
			continue;
		ibProtocolValue& held = copy.Set(entry.m_name);
		const ibProtocolBody* const node = entry.m_value.GetNode();
		if (node != nullptr && held.GetNode() != nullptr)
			ApplyNode(*held.GetNode(), *node);
		else
			held = entry.m_value;
	}
	for (const char* identity : { ibProtocol::ibNode::Type, ibProtocol::ibNode::Id }) {
		if (const ibProtocolValue* const said = patch.Find(identity))
			copy.Set(identity) = *said;
	}

	// Children without ids — whole, as the patch carries them.
	const ibProtocolValue* const children = patch.Find(ibProtocol::ibNode::Children);
	const ibProtocolValue* const whole = patch.Find(ibProtocol::ibNode::ChildrenWhole);
	if (whole != nullptr && whole->GetKind() == ibKind::Bool && whole->AsBool()) {
		copy.Set(ibProtocol::ibNode::Children) = children != nullptr ? *children : ibProtocolValue::List();
		return;
	}
	const ibProtocolBody* const removedIds = ListIn(patch, ibProtocol::ibNode::RemovedIds);
	const ibProtocolBody* const order = ListIn(patch, ibProtocol::ibNode::Order);
	const ibProtocolBody* const patched = children != nullptr ? children->GetList() : nullptr;
	if (patched == nullptr && removedIds == nullptr && order == nullptr)
		return;

	ibProtocolValue& held = copy.Set(ibProtocol::ibNode::Children);
	if (held.GetList() == nullptr)
		held = ibProtocolValue::List();
	std::vector<ibProtocolEntry>& kids = held.GetList()->m_entries;

	// Children by their ids: those gone taken out first; then each the patch carries merged into the one held, or
	// in whole when none is (a child gone and back under its id was taken out above)…
	if (removedIds != nullptr) {
		std::set<long long> ids;
		for (const ibProtocolEntry& id : removedIds->m_entries) {
			if (id.m_value.GetKind() == ibKind::Int)
				ids.insert(id.m_value.AsInt());
		}
		kids.erase(std::remove_if(kids.begin(), kids.end(),
			[&ids](const ibProtocolEntry& kid) { return ids.count(IdOf(kid.m_value)) != 0; }), kids.end());
	}
	if (patched != nullptr) {
		for (const ibProtocolEntry& child : patched->m_entries) {
			const long long id = IdOf(child.m_value);
			const auto kid = std::find_if(kids.begin(), kids.end(),
				[id](const ibProtocolEntry& one) { return IdOf(one.m_value) == id; });
			if (kid == kids.end()) {
				kids.push_back(child);
			}
			else if (const ibProtocolBody* const node = child.m_value.GetNode()) {
				if (kid->m_value.GetNode() == nullptr)
					kid->m_value = ibProtocolValue::Node();
				ApplyNode(*kid->m_value.GetNode(), *node);
			}
		}
	}
	// …in the order the patch gives, when it gives one: whenever the order changed or something came in.
	if (order != nullptr) {
		std::map<long long, std::size_t> position;
		for (std::size_t i = 0; i < order->m_entries.size(); ++i) {
			const ibProtocolValue& id = order->m_entries[i].m_value;
			if (id.GetKind() == ibKind::Int)
				position[id.AsInt()] = i;
		}
		const auto at = [&position](const ibProtocolEntry& kid) {
			const auto found = position.find(IdOf(kid.m_value));
			return found != position.end() ? found->second : position.size();
		};
		std::stable_sort(kids.begin(), kids.end(),
			[&at](const ibProtocolEntry& left, const ibProtocolEntry& right) { return at(left) < at(right); });
	}
}

} // namespace

void ibProtocolNode::Apply(const ibProtocolNode& patch)
{
	if (IsNode() && patch.IsNode())
		ApplyNode(*m_body, *patch.m_body);
}

std::size_t ibProtocolNode::GetChangeCount() const
{
	if (!IsNode())
		return 0;
	// Its entries set, and the entries it removes (NodeRemoved) — not the words that name it and lead to its children.
	std::size_t count = 0;
	for (const ibProtocolEntry& entry : m_body->m_entries) {
		if (!IsStructural(entry.m_name) || entry.m_name == ibProtocol::ibNode::Removed)
			++count;
	}
	return count;
}
