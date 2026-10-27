#include "variantRecord.h"

ibString ibVariantRecord::GetString() const
{
	const ibMetaData* metaData = m_ownerProperty->GetMetaData();
	if (metaData == nullptr) return ibEmptyString;
	ibString strDescr;
	for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* record = metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx));
		if (record == nullptr || !record->IsAllowed())
			continue;
		if (strDescr.IsEmpty()) {
			strDescr = record->GetName();
		}
		else {
			strDescr = strDescr + ibT(", ") + record->GetName();
		}
	}
	return strDescr;
}

//////////////////////////////////////////////

#include "backend/system/value/valueArray.h"

ibValue ibVariantRecord::GetDataValue() const
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