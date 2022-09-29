#include "catalogData.h"
#include "catalogVariant.h"

bool ownerData_t::LoadData(CMemoryReader& dataReader)
{
	unsigned int count = dataReader.r_u32();

	for (unsigned int i = 0; i < count; i++) {
		meta_identifier_t record_id = dataReader.r_u32();
		m_data.insert(record_id);
	}

	return true;
}

bool ownerData_t::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_data.size());
	for (auto record_id : m_data) {
		dataWritter.w_u32(record_id);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

bool ownerData_t::LoadFromVariant(const wxVariant& variant)
{
	wxVariantOwnerData* list =
		dynamic_cast<wxVariantOwnerData*>(variant.GetData());
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

void ownerData_t::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantOwnerData* list = new wxVariantOwnerData(metaData);
	for (auto clsid : m_data) {
		list->SetMetatype(clsid);
	}
	variant = list;
}
