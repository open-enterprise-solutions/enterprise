#include "interfaceHelper.h"

#define interfaceBlock 0x200020

bool ibInterfaceObject::LoadInterface(ibReaderMemory& dataReader)
{
	ibMemoryBuffer buf;
	if (dataReader.r_chunk(interfaceBlock, buf)) {
		std::shared_ptr<ibReaderMemory> dataInterfaceReader(new ibReaderMemory(buf));
		unsigned int countInterface = dataInterfaceReader->r_u32(); m_interfaces.clear();
		for (unsigned int idx = 0; idx < countInterface; idx++) {
			m_interfaces.emplace(dataInterfaceReader->r_s32());
		}
		return true;
	}

	return true;
}

bool ibInterfaceObject::SaveInterface(ibWriterMemory& dataWritter) const
{
	ibWriterMemory dataRoleWritter;
	dataRoleWritter.w_u32(m_interfaces.size());
	for (auto id : m_interfaces) {
		dataRoleWritter.w_s32(id); // interface id
	}
	dataWritter.w_chunk(interfaceBlock, dataRoleWritter.buffer());
	return true;
}
