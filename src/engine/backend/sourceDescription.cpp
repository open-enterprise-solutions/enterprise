#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"
#include "backend/metaData.h"   // ibMetaData::FindAnyObjectByFilter + GetCommonGuid / GetMetaID
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// metaId <-> stable guid lives on ibMetaData::GuidByMetaId / MetaIdByGuid now (shared with
// the meta-description serialiser); guard the null-metaData case here at the call site.

// HEAD (path[0]) is a FORM-LOCAL attribute id — NOT a config metaId — so it is
// stored RAW (a guid round-trip would mis-resolve it config-wide: FindAnyObjectByFilter
// would grab some unrelated metaobject that happens to share the small id, and on a
// metaobject COPY pick up its copy-guid → the binding head "drifts"). The remaining
// hops are real config field/reference metaIds → copy-aware guid round-trip.
bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	srcDesc.ClearSource();
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		if (i == 0) {
			srcDesc.AppendSource((ibSourceId)reader.r_u32());   // raw form-local attribute head
			continue;
		}
		const ibGuid guid(reader.r_stringZ());
		srcDesc.AppendSource(metaData != nullptr ? metaData->MetaIdByGuid(guid) : wxNOT_FOUND);
	}
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	const std::vector<ibSourceId>& path = srcDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (size_t i = 0; i < path.size(); i++) {
		if (i == 0) {
			writer.w_u32((unsigned int)path[i]);   // raw form-local attribute head
			continue;
		}
		writer.w_stringZ(wxString(metaData != nullptr ? metaData->GuidByMetaId(path[i]) : wxNullGuid));
	}
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
