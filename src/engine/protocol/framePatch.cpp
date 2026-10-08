#include "protocolNode.h"
#include "protocol/protocol.h"   // ibProtocolName

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

// The words a node is written with on the wire, and the patch's own — what the patch says of the node and its
// children, never an entry of it.
bool IsStructural(const std::string& name)
{
	return name == ibProtocolName::NodeRemoved || name == ibProtocolName::NodeRemovedIds
		|| name == ibProtocolName::NodeOrder || name == ibProtocolName::NodeChildrenWhole
		|| name == ibProtocolName::NodeChildren || name == ibProtocolName::NodeType || name == ibProtocolName::NodeId;
}

// A child's id — 0 for one that has none.
long long IdOf(const json& node)
{
	if (!node.is_object())
		return 0;
	const auto id = node.find(ibProtocolName::NodeId);
	return id != node.end() && id->is_number() ? id->get<long long>() : 0;
}

void ApplyNode(json& copy, const json& patch)
{
	if (!copy.is_object())
		copy = json::object();

	// The entries gone, first; then each the patch carries — merged when both are nodes.
	const auto gone = patch.find(ibProtocolName::NodeRemoved);
	if (gone != patch.end() && gone->is_array()) {
		for (const json& name : *gone) {
			if (name.is_string())
				copy.erase(name.get<std::string>());
		}
	}
	for (auto entry = patch.begin(); entry != patch.end(); ++entry) {
		if (IsStructural(entry.key()))
			continue;
		const auto held = copy.find(entry.key());
		if (entry->is_object() && held != copy.end() && held->is_object())
			ApplyNode(*held, *entry);
		else
			copy[entry.key()] = *entry;
	}
	for (const char* identity : { ibProtocolName::NodeType, ibProtocolName::NodeId }) {
		const auto said = patch.find(identity);
		if (said != patch.end())
			copy[identity] = *said;
	}

	// Children without ids — whole, as the patch carries them.
	const auto children = patch.find(ibProtocolName::NodeChildren);
	const auto whole = patch.find(ibProtocolName::NodeChildrenWhole);
	if (whole != patch.end() && whole->is_boolean() && whole->get<bool>()) {
		copy[ibProtocolName::NodeChildren] = children != patch.end() ? *children : json::array();
		return;
	}
	const auto removedIds = patch.find(ibProtocolName::NodeRemovedIds);
	const auto order = patch.find(ibProtocolName::NodeOrder);
	if (children == patch.end() && removedIds == patch.end() && order == patch.end())
		return;

	json& held = copy[ibProtocolName::NodeChildren];
	if (!held.is_array())
		held = json::array();
	// The array itself — a json's own iterator is bidirectional, and the order below is a sort.
	json::array_t& kids = held.get_ref<json::array_t&>();

	// Children by their ids: those gone taken out first; then each the patch carries merged into the one held, or
	// in whole when none is (a child gone and back under its id was taken out above)…
	if (removedIds != patch.end() && removedIds->is_array()) {
		std::set<long long> ids;
		for (const json& id : *removedIds) {
			if (id.is_number())
				ids.insert(id.get<long long>());
		}
		kids.erase(std::remove_if(kids.begin(), kids.end(),
			[&ids](const json& kid) { return ids.count(IdOf(kid)) != 0; }), kids.end());
	}
	if (children != patch.end() && children->is_array()) {
		for (const json& patched : *children) {
			const long long id = IdOf(patched);
			const auto kid = std::find_if(kids.begin(), kids.end(), [id](const json& one) { return IdOf(one) == id; });
			if (kid != kids.end())
				ApplyNode(*kid, patched);
			else
				kids.push_back(patched);
		}
	}
	// …in the order the patch gives, when it gives one: whenever the order changed or something came in.
	if (order != patch.end() && order->is_array()) {
		std::map<long long, std::size_t> position;
		for (std::size_t i = 0; i < order->size(); ++i) {
			if ((*order)[i].is_number())
				position[(*order)[i].get<long long>()] = i;
		}
		const auto at = [&position](const json& kid) {
			const auto found = position.find(IdOf(kid));
			return found != position.end() ? found->second : position.size();
		};
		std::stable_sort(kids.begin(), kids.end(), [&at](const json& left, const json& right) { return at(left) < at(right); });
	}
}

} // namespace

void ibProtocolNode::Apply(const ibProtocolNode& patch)
{
	if (m_node != nullptr && patch.m_node != nullptr)
		ApplyNode(*m_node, *patch.m_node);
}

std::size_t ibProtocolNode::GetChangeCount() const
{
	if (m_node == nullptr || !m_node->is_object())
		return 0;
	// Its entries set, and the entries it removes (NodeRemoved) — not the words that name it and lead to its children.
	std::size_t count = 0;
	for (auto entry = m_node->begin(); entry != m_node->end(); ++entry) {
		if (!IsStructural(entry.key()) || entry.key() == ibProtocolName::NodeRemoved)
			++count;
	}
	return count;
}
