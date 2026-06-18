#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"
#include "backend/metaData.h"   // ibMetaData::FindAnyObjectByFilter + GetCommonGuid / GetMetaID
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// metaId <-> stable guid lives on ibMetaData::GuidByMetaId / MetaIdByGuid now (shared with
// the meta-description serialiser); guard the null-metaData case here at the call site.

bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	srcDesc.ClearSource();
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		const ibGuid guid(reader.r_stringZ());
		srcDesc.AppendSource(metaData != nullptr ? metaData->MetaIdByGuid(guid) : wxNOT_FOUND);
	}
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	const std::vector<ibMetaID>& path = srcDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (const ibMetaID& id : path)
		writer.w_stringZ(wxString(metaData != nullptr ? metaData->GuidByMetaId(id) : wxNullGuid));
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — Binary blob (the copy-aware GUID round-trip stays in the byte path).
// The byte reader / writer is contained here, not in the property.

bool ibSourceDescriptionMemory::ReadNode(const ibDataValue& value, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, srcDesc, metaData);
	}
	return true;
}

bool ibSourceDescriptionMemory::WriteNode(ibDataValue& value, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	ibWriterMemory writer;
	if (!SaveData(writer, srcDesc, metaData))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
