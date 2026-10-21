#include "dataProtocol.h"

#include "core/serialize/jsonProvider.h"
#include "core/description/compositionDescription.h"   // ibCompositionNodeClsid — a composition's part by its name

bool ibReadProtocolNode(const ibProtocolNode& wire, ibDataNode& node)
{
	node = ibDataNode();
	if (!wire.IsNode())
		return false;

	// A NODE'S TYPE COMES BACK BY ITS NAME where the server named it in words — a composition's parts are written so
	// (ibRpcRenderNode's floor: "CompositionAppearance") — and is read back as the server reads it (ibRpcInstallTypeLookup):
	// unread, it stayed 0, and a setting's appearance was not recognised for one.
	ibJsonProvider provider;
	provider.SetTypeLookup([](const ibString& name) { return ibCompositionNodeClsid(name); });
	return provider.Read(wire.Write(), node);
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
