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
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++)
		cmdDesc.AppendCommand((ibMetaID)reader.r_u32());
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
// node form — Binary blob. The byte reader / writer is contained here, not in the property.

bool ibCommandDescriptionMemory::ReadNode(const ibDataValue& value, ibCommandDescription& cmdDesc)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, cmdDesc);
	}
	return true;
}

bool ibCommandDescriptionMemory::WriteNode(ibDataValue& value, const ibCommandDescription& cmdDesc)
{
	ibWriterMemory writer;
	if (!SaveData(writer, cmdDesc))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
