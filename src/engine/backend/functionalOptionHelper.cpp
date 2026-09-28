#include "functionalOptionHelper.h"

// Its own chunk id, next to the interface and composition blocks but never the same one — the
// three sets are written and read independently.
#define functionalOptionBlock 0x200022

bool ibFunctionalOptionObject::LoadFunctionalOptions(ibReaderMemory& dataReader)
{
	wxMemoryBuffer buf;
	if (dataReader.r_chunk(functionalOptionBlock, buf)) {
		std::shared_ptr<ibReaderMemory> dataOptionReader(new ibReaderMemory(buf));
		unsigned int countOption = dataOptionReader->r_u32(); m_functionalOptions.clear();
		for (unsigned int idx = 0; idx < countOption; idx++) {
			m_functionalOptions.emplace(dataOptionReader->r_s32());
		}
		return true;
	}

	// An absent chunk is not an error: configurations written before this mechanism
	// existed simply belong to no option.
	return true;
}

bool ibFunctionalOptionObject::SaveFunctionalOptions(ibWriterMemory& dataWritter) const
{
	ibWriterMemory dataOptionWritter;
	dataOptionWritter.w_u32(m_functionalOptions.size());
	for (auto id : m_functionalOptions) {
		dataOptionWritter.w_s32(id); // functional option id
	}
	dataWritter.w_chunk(functionalOptionBlock, dataOptionWritter.buffer());
	return true;
}
