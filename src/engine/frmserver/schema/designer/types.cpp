#include "types.h"

#include <vector>

#include "backend/mcp/mcpTool.h"
#include "core/serialize/dataBuilder.h"

void ibSchemaTypes::Build(const ibSession& /*session*/, const ibDataNode& args, ibDataNode& result) const
{
	const ibString name = args.GetValue<ibString>(ibProtocol::ibSchema::ibTypes::Name);

	if (name.IsEmpty()) {
		std::vector<ibDataValue> types;
		ibMcpLanguageTypes(args.GetValue<ibString>(ibProtocol::ibSchema::ibTypes::Query), types);

		ibDataNode& rows = result.Child(ibProtocol::ibSchema::ibTypes::Types);
		ibMetaID idx = 0;
		for (const ibDataValue& type : types) {
			if (const auto listed = type.AsChild())
				rows.AddChild(0, idx++).SetValue(ibProtocol::ibSchema::ibTypes::Name, listed->GetValue<ibString>(ibT("name")));
		}
		return;
	}

	const ibMcpTool* const typeMembers = ibFindMcpTool(ibT("type_members"));
	if (typeMembers == nullptr)
		return;

	ibDataNode asked;
	asked.SetValue(ibT("name"), name);
	ibDataNode members;
	ibString refusal;
	if (!typeMembers->Call(asked, members, refusal)) {
		result.SetValue(ibProtocol::ibSchema::ibTypes::Note, refusal);
		return;
	}

	ibDataNode& methods = result.Child(ibProtocol::ibSchema::ibTypes::Methods);
	ibMetaID idx = 0;
	if (const auto listed = members.FindField(ibT("methods"))) {
		for (const ibDataValue& method : listed->AsArray()) {
			const auto one = method.AsChild();
			if (!one)
				continue;
			ibDataNode& row = methods.AddChild(0, idx++);
			row.SetValue(ibProtocol::ibSchema::ibTypes::Name, one->GetValue<ibString>(ibT("name")));
			row.SetValue(ibProtocol::ibSchema::ibTypes::Signature, one->GetValue<ibString>(ibT("signature")));
		}
	}

	ibDataNode& properties = result.Child(ibProtocol::ibSchema::ibTypes::Properties);
	idx = 0;
	if (const auto listed = members.FindField(ibT("properties"))) {
		for (const ibDataValue& property : listed->AsArray())
			properties.AddChild(0, idx++).SetValue(ibProtocol::ibSchema::ibTypes::Name, property.AsString());
	}
}
