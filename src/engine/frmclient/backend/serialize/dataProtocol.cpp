#include "dataProtocol.h"

#include "core/serialize/jsonProvider.h"
#include "core/description/compositionDescription.h"   // ibCompositionNodeClsid — a composition's part by its name
#include "protocol/protocol.h"                          // ibProtocol::ibNode::Children — the reader's to descend into
#include "protocol/protocolValue.h"                     // the wire's tree, walked where it lies

namespace {

// The engine's reader, as the server reads its nodes — and A NODE'S TYPE COMES BACK BY ITS NAME where the server named
// it in words: a composition's parts are written so (ibRpcRenderNode's floor: "CompositionAppearance") and read back as
// the server reads them (ibRpcInstallTypeLookup); unread, it stayed 0, and a setting's appearance was not recognised.
ibJsonProvider Reader()
{
	ibJsonProvider reader;
	reader.SetTypeLookup([](const ibString& name) { return ibCompositionNodeClsid(name); });
	return reader;
}

bool ReadNode(const ibJsonProvider& reader, const ibProtocolBody& wire, ibDataNode& node);

// A value of the wire as the engine's — what the server's JSON reader makes of the same text: no text written for it
// and read again (the inspector's rows were three quarters of a control's receiving, 2026-10-22).
bool ReadValue(const ibJsonProvider& reader, const ibProtocolValue& wire, ibDataValue& value)
{
	switch (wire.GetKind()) {
	case ibProtocolValue::ibKind::Null:
		value = ibDataValue();
		return true;
	case ibProtocolValue::ibKind::Bool:
		value = ibDataValue::Bool(wire.AsBool());
		return true;
	case ibProtocolValue::ibKind::Int:
		value = ibDataValue::Number(ibNumber(wire.AsInt()));
		return true;
	case ibProtocolValue::ibKind::Number: {
		ibNumber number;
		if (!number.FromString(wire.AsText()))
			return false;
		value = ibDataValue::Number(number);
		return true;
	}
	case ibProtocolValue::ibKind::Text:
		value = ibJsonProvider::ReadString(wire.AsText());
		return true;
	case ibProtocolValue::ibKind::Node: {
		std::shared_ptr<ibDataNode> child = std::make_shared<ibDataNode>();
		if (!ReadNode(reader, *wire.GetNode(), *child))
			return false;
		value = ibDataValue::Child(child);
		return true;
	}
	case ibProtocolValue::ibKind::List: {
		std::vector<ibDataValue> items;
		items.reserve(wire.GetList()->m_entries.size());
		for (const ibProtocolEntry& item : wire.GetList()->m_entries) {
			ibDataValue read;
			if (!ReadValue(reader, item.m_value, read))
				return false;
			items.push_back(std::move(read));
		}
		value = ibDataValue::Array(std::move(items));
		return true;
	}
	}
	return false;
}

// A node of the wire as the engine's: its children descended into here, every other entry by the reader's rule
// (ibJsonProvider::ReadEntry) — the one its text reader keeps.
bool ReadNode(const ibJsonProvider& reader, const ibProtocolBody& wire, ibDataNode& node)
{
	// The engine's string, told from a name by its length first — against the UTF-8 literal every name was decoded.
	static const ibString kChildren = ibProtocol::ibNode::Children;
	for (const ibProtocolEntry& entry : wire.m_entries) {
		if (entry.m_name == kChildren) {
			const ibProtocolBody* const children = entry.m_value.GetList();
			if (children == nullptr)
				return false;
			for (const ibProtocolEntry& item : children->m_entries) {
				const ibProtocolBody* const child = item.m_value.GetNode();
				ibDataNode scratch;
				if (child == nullptr || !ReadNode(reader, *child, scratch))
					return false;
				node.Children().emplace_back(std::move(scratch));
			}
			continue;
		}
		ibDataValue value;
		if (!ReadValue(reader, entry.m_value, value) || !reader.ReadEntry(node, entry.m_name, std::move(value)))
			return false;
	}
	return true;
}

} // namespace

bool ibReadProtocolNode(const ibProtocolNode& wire, ibDataNode& node)
{
	node = ibDataNode();
	if (!wire.IsNode())
		return false;
	return ReadNode(Reader(), *wire.GetBody(), node);
}

bool ibReadProtocolValue(const ibProtocolNode& wire, const ibString& name, ibDataValue& value)
{
	value = ibDataValue();
	const ibProtocolValue* const entry = wire.IsNode() ? wire.GetBody()->Find(name) : nullptr;
	return entry != nullptr && ReadValue(Reader(), *entry, value);
}

void ibWriteProtocolNode(const ibDataNode& node, ibProtocolNode wire)
{
	ibJsonProvider provider;
	provider.SetCompact(true);

	ibString text;
	provider.Write(node, text);

	ibProtocolNode written;
	if (!ibProtocolNode::Read(text, written))
		return;
	for (const auto& [name, entry] : written.Entries())
		wire.SetValue(name, entry);
}
