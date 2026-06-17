#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"
#include "backend/metaData.h"   // ibMetaData::FindAnyObjectByFilter + GetCommonGuid / GetMetaID

////////////////////////////////////////////////////////////////////////

// metaId -> the metaobject's copy-aware guid (GetCommonGuid: copy-guid during a copy).
static ibGuid GuidByMetaId(const ibMetaData* metaData, const ibMetaID& id)
{
	const ibValueMetaObject* meta = metaData != nullptr ? metaData->FindAnyObjectByFilter(id, true) : nullptr;
	return meta != nullptr && meta->IsAllowed() ? meta->GetCommonGuid() : wxNullGuid;
}

// guid -> the live metaId (resolves a pasted copy by its copy-guid, or a normal object by guid).
static ibMetaID MetaIdByGuid(const ibMetaData* metaData, const ibGuid& guid)
{
	if (!guid.isValid()) return wxNOT_FOUND;
	const ibValueMetaObject* meta = metaData != nullptr ? metaData->FindAnyObjectByFilter(guid, true) : nullptr;
	return meta != nullptr && meta->IsAllowed() ? meta->GetMetaID() : wxNOT_FOUND;
}

////////////////////////////////////////////////////////////////////////

bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	srcDesc.ClearSource();
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++)
		srcDesc.AppendSource(MetaIdByGuid(metaData, ibGuid(reader.r_stringZ())));
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, ibSourceDescription& srcDesc, const ibMetaData* metaData)
{
	const std::vector<ibMetaID>& path = srcDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (const ibMetaID& id : path)
		writer.w_stringZ(wxString(GuidByMetaId(metaData, id)));
	return true;
}
