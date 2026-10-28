#include "dataProtocol.h"

#include "core/serialize/jsonProvider.h"
#include "core/description/compositionDescription.h"   // ibCompositionNodeClsid — a composition's part by its name

namespace {

// A NODE'S TYPE COMES BACK BY ITS NAME where it was kept as a word — a node a client made, a text read: a composition's
// parts as the server reads them (ibRpcInstallTypeLookup). A packet's nodes carry their clsids, and the walk finds no
// word in them.
ibJsonProvider Reader()
{
	ibJsonProvider reader;
	reader.SetTypeLookup([](const ibString& name) { return ibCompositionNodeClsid(name); });
	return reader;
}

} // namespace

bool ibReadProtocolNode(const ibProtocolNode& wire, ibDataNode& node)
{
	const ibDataNode* const read = wire.GetBody();
	node = read != nullptr ? *read : ibDataNode();
	if (read == nullptr)
		return false;
	Reader().LookupTypes(node);
	return true;
}

bool ibReadProtocolValue(const ibProtocolNode& wire, const ibString& name, ibDataValue& value)
{
	value = ibDataValue();
	const ibDataNode* const read = wire.GetBody();
	const auto found = read != nullptr ? read->FindField(name) : std::nullopt;
	if (!found)
		return false;
	value = *found;
	// A node under the name, its types looked up as ibReadProtocolNode looks them up — a new node only where one was a word.
	if (value.Kind() == ibDataKind::Child) {
		const ibDataNode child = *value.AsChild();
		ibDataNode looked = child;
		Reader().LookupTypes(looked);
		if (looked != child)
			value = ibDataValue::Child(std::make_shared<ibDataNode>(std::move(looked)));
	}
	return true;
}

void ibWriteProtocolNode(const ibDataNode& node, ibProtocolNode wire)
{
	// Entry by entry, as the JSON view names them — the node's type and id, its fields, its children: what the wire's
	// node had of them is replaced, the rest kept.
	for (const auto& [name, entry] : ibProtocolNode(node).Entries())
		wire.SetValue(name, entry);
}
