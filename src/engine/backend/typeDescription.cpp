#include "typeDescription.h"
#include "backend/fileSystem/fs.h"

////////////////////////////////////////////////////////////////////////

bool ibTypeDescriptionMemory::LoadData(ibReaderMemory& reader, ibTypeDescription& typeDesc)
{
	typeDesc.ClearMetaType();
	unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) 
		typeDesc.m_listTypeClass.emplace_back(reader.r_u64());
	reader.r(&typeDesc.m_typeData, sizeof(ibTypeDescription::ibTypeData));
	return true;
}

bool ibTypeDescriptionMemory::SaveData(ibWriterMemory& writer, ibTypeDescription& typeDesc)
{
	writer.w_u32(typeDesc.m_listTypeClass.size());
	for (auto clsid : typeDesc.m_listTypeClass) writer.w_u64(clsid);
	writer.w(&typeDesc.m_typeData, sizeof(ibTypeDescription::ibTypeData));
	return true;
}

////////////////////////////////////////////////////////////////////////

bool ibMetaDescriptionMemory::LoadData(ibReaderMemory& reader, ibMetaDescription& metaDesc)
{
	unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) 
		metaDesc.m_listMetaClass.emplace_back(reader.r_u32());
	return true;
}

bool ibMetaDescriptionMemory::SaveData(ibWriterMemory& writer, ibMetaDescription& metaDesc)
{
	writer.w_u32(metaDesc.m_listMetaClass.size());
	for (auto id : metaDesc.m_listMetaClass) writer.w_u32(id);
	return true;
}
