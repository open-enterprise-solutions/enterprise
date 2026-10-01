#include "compositionHelper.h"

// Its own chunk id, next to the interface block but never the same one — the two sets are
// written and read independently, so an object can be in a section AND carry a common
// attribute without either mechanism seeing the other's ids.
#define compositionBlock 0x200021

bool ibCompositionObject::LoadComposition(ibReaderMemory& dataReader)
{
	wxMemoryBuffer buf;
	if (dataReader.r_chunk(compositionBlock, buf)) {
		std::shared_ptr<ibReaderMemory> dataCompositionReader(new ibReaderMemory(buf));
		unsigned int countComposition = dataCompositionReader->r_u32(); m_compositions.clear();
		for (unsigned int idx = 0; idx < countComposition; idx++) {
			m_compositions.emplace(dataCompositionReader->r_s32());
		}
		return true;
	}

	// An absent chunk is not an error: configurations written before this mechanism
	// existed simply have no compositions.
	return true;
}

bool ibCompositionObject::SaveComposition(ibWriterMemory& dataWritter) const
{
	ibWriterMemory dataCompositionWritter;
	dataCompositionWritter.w_u32(m_compositions.size());
	for (auto id : m_compositions) {
		dataCompositionWritter.w_s32(id); // composition id
	}
	dataWritter.w_chunk(compositionBlock, dataCompositionWritter.buffer());
	return true;
}
