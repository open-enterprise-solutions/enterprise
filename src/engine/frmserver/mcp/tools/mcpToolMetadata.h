#ifndef __MCP_TOOL_METADATA_H__
#define __MCP_TOOL_METADATA_H__

// What the metadata tools share: those that read (mcpToolMetadata.cpp, common) and those that change the
// configuration (designer/mcpToolMetadataWrite.cpp).

#include "frmserver/mcp/mcpTool.h"

#include "backend/metadataConfiguration.h"

namespace ibMcpMetadata {

using ibArg = ibMcpTool::ibMcpArgument;

// Every tool here needs the same first sentence, and the same refusal when
// there is nothing open to answer about.
inline ibMetaData* OpenConfiguration(ibString& refusal)
{
	ibMetaData* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	return metaData;
}

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
inline const ibArg& ArgKind()
{
	// 🛑 THE EXAMPLES ENDED IN `Enum`, WHICH IS NOT A KIND ANYTHING MAY BE ADDED AS. The metaobject
	// is an `Enumeration`; `Enum` is one VALUE inside one. A caller following this description to
	// the letter was refused — "A Enum cannot be added to 'Configuration'" — by the tool whose own
	// text had just told them to write it (2026-09-05, building a delivery note over this server).
	// An example in a description is read as a promise, and naming a kind that does not exist costs
	// more than naming none: the caller doubts the platform rather than the sentence.
	static const ibArg s_a(ibT("kind"), ibArg::Kind::Text,
		ibMcpText("The kind, spelled the way a script writes it: Catalog, Document, Enumeration, "
			  "InformationRegister, AccumulationRegister, Report, DataProcessor... The kinds that "
			  "live INSIDE something are named the same way - Attribute, TabularSection, and the "
			  "`Enum` that is one value of an Enumeration. metadata_accepts answers the exact list "
			  "a given parent takes, which beats trying kinds until one is let through."), /*required*/ true);
	return s_a;
}

inline const ibArg& ArgId()
{
	static const ibArg s_a(ibT("id"), ibArg::Kind::Whole,
		ibMcpText("The object's identity, as NodeId in a previous answer. Survives a rename, "
			  "and finds an attribute or a tabular section as readily as its owner."));
	return s_a;
}

inline const ibArg& ArgName()
{
	static const ibArg s_a(ibT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it. Omitted, it gets the same generated name a click would give it."));
	return s_a;
}

} // namespace ibMcpMetadata

#endif
