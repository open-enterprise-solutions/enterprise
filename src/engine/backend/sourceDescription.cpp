#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"                // ibReaderMemory / ibWriterMemory
#include "backend/serialize/dataBuilder.h"        // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// A DUMB serializer: it writes / reads the hop path AS-IS, nothing else. No metadata, no copy awareness — the id
// path is stored verbatim ([count : u32][ id : u32, type : u64 ] per hop). What the ids MEAN and how a paste
// transforms them is NOT its business: that contract lives in the OWNER (ibPropertySource::CopyNodeValue /
// PasteNodeValue), which processes the path before writing / after reading.

bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc)
{
	srcDesc.ClearSource();
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		const ibSourceId id = (ibSourceId)reader.r_u32();
		const ibClassID type = (ibClassID)reader.r_u64();   // expected type (undefined where the hop imposes none)
		srcDesc.AppendSource(id, type);
	}
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, const ibSourceDescription& srcDesc)
{
	const std::vector<ibSourceHop>& path = srcDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (const ibSourceHop& hop : path) {
		writer.w_u32((unsigned int)hop.m_id);
		writer.w_u64((unsigned wxLongLong_t)hop.m_type);
	}
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — Binary blob. The byte reader / writer is contained here, not in the property.

bool ibSourceDescriptionMemory::ReadNode(const ibDataValue& value, ibSourceDescription& srcDesc)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, srcDesc);
	}
	return true;
}

bool ibSourceDescriptionMemory::WriteNode(ibDataValue& value, const ibSourceDescription& srcDesc)
{
	ibWriterMemory writer;
	if (!SaveData(writer, srcDesc))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
