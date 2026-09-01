#include "commandDescription.h"
#include "backend/fileSystem/fs.h"                // ibReaderMemory / ibWriterMemory
#include "backend/serialize/dataBuilder.h"        // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// A DUMB serializer: it writes / reads the id path AS-IS, nothing else. No metadata, no copy awareness — the
// id path is stored verbatim ([count : u32][ id : u32 ] per hop). What the ids MEAN (a source vs a command) is
// resolved by the command OBJECT at walk time, exactly like ibSourceDescriptionMemory leaves meaning to the walk.

bool ibCommandDescriptionMemory::LoadData(ibReaderMemory& reader, ibCommandDescription& cmdDesc)
{
	cmdDesc.ClearCommand();
	cmdDesc.SetCommandType(ibInterfaceCommandType_Default);
	// The stored count is a CLAIM — a blob can be shorter than it says. The eof-guard below already
	// admits that (it lets a pre-object-item blob end early); the loop has to admit it too, or a
	// short one walks the reader past the end of the buffer. See sourceDescription.cpp for the same
	// guard and the crash that asked for it.
	if (reader.elapsed() < static_cast<int>(sizeof(u32)))
		return true;
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		if (reader.elapsed() < static_cast<int>(sizeof(u32)))
			break;
		cmdDesc.AppendCommand((ibMetaID)reader.r_u32());
	}
	// Object-item command TYPE trails the id path (eof-guard: a pre-object-item blob leaves it Default).
	if (!reader.eof())
		cmdDesc.SetCommandType((ibInterfaceCommandType)reader.r_u32());
	return true;
}

bool ibCommandDescriptionMemory::SaveData(ibWriterMemory& writer, const ibCommandDescription& cmdDesc)
{
	const std::vector<ibCommandHop>& path = cmdDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (const ibCommandHop& hop : path)
		writer.w_u32((unsigned int)hop.m_id);
	writer.w_u32((unsigned int)cmdDesc.GetCommandType());   // object-item command type (Default for real commands)
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — A STRUCTURE, like everything else that has parts.
//
// ⭐ WHAT A CONTROL IS WIRED TO IS SOMETHING TO READ. This travelled as an opaque
// Binary block, which is the tail of the migration onto the node rather than a
// decision: a node exists so every provider renders the same value without
// knowing what it means, and a blob defeats exactly that — the JSON view of a
// form showed a base64 smear where "which command does this button run" was.
//
// A command reference is a PATH, the same shape a source binding is: hops to walk,
// plus the item type for the object-item commands (Default for a real command).
//
// ⚠ NO FALLBACK TO THE BLOB, deliberately (Max, 2026-08-30: the binary form is a
// remnant and is not supported). A format with two shapes is a format that will
// drift. LoadData / SaveData below stay — they serve the clipboard's own
// transient form, which never reaches a stored file.

namespace {

const wxChar* const kHopsField = wxT("hops");
const wxChar* const kTypeField = wxT("commandType");

} // namespace

bool ibCommandDescriptionMemory::ReadNode(const ibDataValue& value, ibCommandDescription& cmdDesc)
{
	const std::shared_ptr<ibDataNode>& node = value.AsChild();
	if (!node)
		return true;   // wired to nothing — an ordinary state

	if (const ibDataValue* hops = node->FindField(kHopsField)) {
		for (const ibDataValue& hop : hops->AsArray())
			cmdDesc.AppendCommand((ibMetaID)hop.AsInt());
	}

	cmdDesc.SetCommandType((ibInterfaceCommandType)node->GetValue<s32>(kTypeField));
	return true;
}

bool ibCommandDescriptionMemory::WriteNode(ibDataValue& value, const ibCommandDescription& cmdDesc)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	std::vector<ibDataValue> hops;
	for (const ibCommandHop& hop : cmdDesc.GetPath())
		hops.push_back(ibDataValue::Int((s64)hop.m_id));

	node->AddField(kHopsField, ibDataValue::Array(hops));
	node->AddField(kTypeField, ibDataValue::Int((s64)cmdDesc.GetCommandType()));

	value = ibDataValue::Child(node);
	return true;
}
