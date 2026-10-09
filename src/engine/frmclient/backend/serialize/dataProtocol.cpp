#include "dataProtocol.h"

#include "core/serialize/jsonProvider.h"
#include "frmclient/backend/compositionDescription.h"   // ibCompositionNodeClsid — a composition's part by its name

bool ibReadProtocolNode(const ibProtocolNode& wire, ibDataNode& node)
{
	node = ibDataNode();
	if (!wire.IsNode())
		return false;

	std::string text = wire.Write();
	ibReader reader(text.data(), static_cast<int>(text.size()));
	// A NODE'S TYPE COMES BACK BY ITS NAME where the server named it in words — a composition's parts are written so
	// (ibRpcRenderNode's floor: "CompositionAppearance") — and is read back as the server reads it (ibRpcInstallTypeLookup):
	// unread, it stayed 0, and a setting's appearance was not recognised for one.
	ibJsonProvider provider;
	provider.SetTypeLookup([](const wxString& name) { return ibCompositionNodeClsid(name); });
	return provider.Read(reader, node);
}

void ibWriteProtocolNode(const ibDataNode& node, ibProtocolNode wire)
{
	ibJsonProvider provider;
	provider.SetCompact(true);

	ibWriterMemory writer;
	provider.Write(node, writer);

	ibProtocolNode written;
	if (!ibProtocolNode::Read(std::string(reinterpret_cast<const char*>(writer.pointer()), writer.size()), written))
		return;
	for (const auto& [name, entry] : written.Entries())
		wire.SetValue(name.utf8_str(), entry);
}
