#include "variantRecord.h"

wxString ibVariantDataRecord::MakeString() const
{
	const ibMetaData* metaData = m_ownerProperty->GetMetaData();
	if (metaData == nullptr) return wxEmptyString;
	wxString strDescr;
	for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* record = metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx));
		if (record == nullptr || !record->IsAllowed())
			continue;
		if (strDescr.IsEmpty()) {
			strDescr = record->GetName();
		}
		else {
			strDescr = strDescr + wxT(", ") + record->GetName();
		}
	}
	return strDescr;
}

//////////////////////////////////////////////

#include "backend/system/value/valueArray.h"

ibValue ibVariantDataRecord::GetDataValue() const
{
	ibValueArray* valueArr = new ibValueArray();
	const ibMetaData* metaData = m_ownerProperty->GetMetaData();
	if (metaData != nullptr) {
		for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) { 
			valueArr->Add(metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx))); 
		}
	}
	return valueArr;
}

//////////////////////////////////////////////