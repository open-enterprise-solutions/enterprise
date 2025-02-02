#include "objectVariant.h"

wxString wxVariantGenerationData::MakeString() const
{
	wxString description;
	for (auto clsid : m_genData) {
		IMetaObjectRecordDataMutableRef* record =
			dynamic_cast<IMetaObjectRecordDataMutableRef*>(m_metaData->GetMetaObject(clsid));
		if (record == nullptr || !record->IsAllowed())
			continue;
		if (description.IsEmpty()) {
			description = record->GetName();
		}
		else {
			description = description +
				", " + record->GetName();
		}
	}
	return description;
}