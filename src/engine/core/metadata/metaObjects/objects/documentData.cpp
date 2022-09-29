#include "documentData.h"
#include "documentVariant.h"

bool recordData_t::LoadData(CMemoryReader& dataReader)
{
	unsigned int count = dataReader.r_u32();

	for (unsigned int i = 0; i < count; i++) {
		meta_identifier_t record_id = dataReader.r_u32();
		m_data.insert(record_id);
	}

	return true;
}

bool recordData_t::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_data.size());

	for (auto record_id : m_data) {
		dataWritter.w_u32(record_id);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

bool recordData_t::LoadFromVariant(const wxVariant& variant)
{
	wxVariantRecordData* list =
		dynamic_cast<wxVariantRecordData*>(variant.GetData());
	if (list == NULL)
		return false;
	m_data.clear();
	for (unsigned int idx = 0; idx < list->GetCount(); idx++) {
		m_data.insert(
			list->GetByIdx(idx)
		);
	}
	return true;
}

void recordData_t::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantRecordData* list = new wxVariantRecordData(metaData);
	for (auto record_id : m_data) {
		list->SetMetatype(record_id);
	}
	variant = list;
}
