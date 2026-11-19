#include "featureOptionHelper.h"

// Its own chunk id, next to the interface and composition blocks but never the same one — the
// three sets are written and read independently.
#define featureOptionBlock 0x200022

bool ibFeatureOptionObject::LoadFeatureOptions(ibReaderMemory& dataReader)
{
	ibMemoryBuffer buf;
	if (dataReader.r_chunk(featureOptionBlock, buf)) {
		std::shared_ptr<ibReaderMemory> dataOptionReader(new ibReaderMemory(buf));
		unsigned int countOption = dataOptionReader->r_u32(); m_featureOptions.clear();
		for (unsigned int idx = 0; idx < countOption; idx++) {
			m_featureOptions.emplace(dataOptionReader->r_s32());
		}
		return true;
	}

	// An absent chunk is not an error: applications written before this mechanism
	// existed simply belong to no option.
	return true;
}

bool ibFeatureOptionObject::SaveFeatureOptions(ibWriterMemory& dataWritter) const
{
	ibWriterMemory dataOptionWritter;
	dataOptionWritter.w_u32(m_featureOptions.size());
	for (auto id : m_featureOptions) {
		dataOptionWritter.w_s32(id); // feature option id
	}
	dataWritter.w_chunk(featureOptionBlock, dataOptionWritter.buffer());
	return true;
}
