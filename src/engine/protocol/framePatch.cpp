#include "protocolNode.h"
#include "protocol/protocol.h"   // ibProtocol::ibNode

#include <algorithm>
#include <map>
#include <set>

namespace {

// The patch's own words — what it says of the node and its children, never an entry of it.
bool IsStructural(const ibString& name)
{
	return name == ibProtocol::ibNode::Removed || name == ibProtocol::ibNode::RemovedIds
		|| name == ibProtocol::ibNode::Order || name == ibProtocol::ibNode::ChildrenWhole;
}

// The names the patch takes out of the node (NodeRemoved).
std::vector<ibString> RemovedOf(const ibDataNode& patch)
{
	std::vector<ibString> removed;
	if (const auto gone = patch.FindField(ibProtocol::ibNode::Removed); gone && gone->Kind() == ibDataKind::Array) {
		for (const ibDataValue& name : gone->AsArray()) {
			if (name.Kind() == ibDataKind::String)
				removed.push_back(name.AsString());
		}
	}
	return removed;
}

// …and the ids of a list of them (NodeRemovedIds, NodeOrder), in its order.
std::vector<long long> IdsOf(const ibDataNode& patch, const char* name)
{
	std::vector<long long> ids;
	if (const auto list = patch.FindField(name); list && list->Kind() == ibDataKind::Array) {
		for (const ibDataValue& id : list->AsArray()) {
			if (id.Kind() == ibDataKind::Number)
				ids.push_back(id.AsInt());
		}
	}
	return ids;
}

// THE NODE THE FRAME HOLDS, MADE INTO THE ONE DRAWN NOW — made anew of the two: what the patch leaves is copied as it
// lies, a node it changes is made the same way.
ibDataNode Applied(const ibDataNode& held, const ibDataNode& patch)
{
	ibDataNode made(patch.GetClsid() != ibClassID(0) ? patch.GetClsid() : held.GetClsid(),
		patch.GetMetaId() != 0 ? patch.GetMetaId() : held.GetMetaId());
	if (held.HasRawData())
		made.SetRawData(held.RawData());

	// The entries gone first; then each held in its place — what the patch says of it in place of it, merged when both
	// are nodes — and those the patch says that the frame does not hold, at the end.
	const std::vector<ibString> removed = RemovedOf(patch);
	const auto gone = [&removed](const ibString& name) {
		return std::find(removed.begin(), removed.end(), name) != removed.end();
	};
	for (const auto& field : held.Fields()) {
		if (gone(field.first))
			continue;
		const auto said = patch.FindField(field.first);
		if (!said)
			made.AddField(field.first, field.second);
		else if (said->Kind() == ibDataKind::Child && field.second.Kind() == ibDataKind::Child)
			made.Child(field.first) = Applied(*field.second.AsChild(), *said->AsChild());
		else
			made.AddField(field.first, *said);
	}
	for (const auto& field : patch.Fields()) {
		if (!IsStructural(field.first) && (gone(field.first) || !held.FindField(field.first)))
			made.AddField(field.first, field.second);
	}

	// Children without ids — whole, as the patch carries them.
	if (const auto whole = patch.FindField(ibProtocol::ibNode::ChildrenWhole);
		whole && whole->Kind() == ibDataKind::Bool && whole->AsBool()) {
		for (const ibDataNode& child : patch.Children())
			made.AddChild(child);
		return made;
	}

	std::vector<ibDataNode> kids;
	for (const ibDataNode& kid : held.Children())
		kids.push_back(kid);

	// Children by their ids: those gone taken out first; then each the patch carries merged into the one held, or in
	// whole when none is (a child gone and back under its id was taken out above)…
	const std::vector<long long> removedIds = IdsOf(patch, ibProtocol::ibNode::RemovedIds);
	if (!removedIds.empty()) {
		const std::set<long long> ids(removedIds.begin(), removedIds.end());
		kids.erase(std::remove_if(kids.begin(), kids.end(),
			[&ids](const ibDataNode& kid) { return ids.count(kid.GetMetaId()) != 0; }), kids.end());
	}
	for (const ibDataNode& child : patch.Children()) {
		const auto kid = std::find_if(kids.begin(), kids.end(),
			[&child](const ibDataNode& one) { return one.GetMetaId() == child.GetMetaId(); });
		if (kid == kids.end())
			kids.push_back(child);
		else
			*kid = Applied(*kid, child);
	}
	// …in the order the patch gives, when it gives one: whenever the order changed or something came in.
	if (patch.FindField(ibProtocol::ibNode::Order)) {
		const std::vector<long long> order = IdsOf(patch, ibProtocol::ibNode::Order);
		std::map<long long, std::size_t> position;
		for (std::size_t i = 0; i < order.size(); ++i)
			position[order[i]] = i;
		const auto at = [&position](const ibDataNode& kid) {
			const auto found = position.find(kid.GetMetaId());
			return found != position.end() ? found->second : position.size();
		};
		std::stable_sort(kids.begin(), kids.end(),
			[&at](const ibDataNode& left, const ibDataNode& right) { return at(left) < at(right); });
	}
	for (const ibDataNode& kid : kids)
		made.AddChild(kid);
	return made;
}

} // namespace

void ibProtocolNode::Apply(const ibProtocolNode& patch)
{
	if (!IsNode() || !patch.IsNode())
		return;

	// A tree of its own — what the frame's names of its types are, and the patch's beside them.
	auto tree = std::make_shared<ibProtocolTree>();
	tree->m_made = m_tree->m_made;
	tree->m_root = Applied(Held(), patch.Held());
	tree->NameTypes(m_tree->m_types);
	tree->NameTypes(patch.m_tree->m_types);
	m_tree = std::move(tree);
	m_node = &m_tree->m_root;
	m_read = ibDataNode();
}

std::size_t ibProtocolNode::GetChangeCount() const
{
	if (!IsNode())
		return 0;
	// Its entries set, and the entries it removes (NodeRemoved) — not the words that lead to its children.
	std::size_t count = 0;
	for (const auto& field : Held().Fields()) {
		if (!IsStructural(field.first) || field.first == ibProtocol::ibNode::Removed)
			++count;
	}
	return count;
}
