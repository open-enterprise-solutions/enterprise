#include "catalogVariant.h"

wxString wxVariantOwnerData::MakeString() const
{
	wxString description;
	for (auto clsid : m_ownerData) {
		IMetaObjectRecordDataRef* record =
			dynamic_cast<IMetaObjectRecordDataRef*>(m_metaData->GetMetaObject(clsid));
		if (record == NULL || !record->IsAllowed())
			continue;
		if (description.IsEmpty()) {
			description = record->GetName();
		}
		else {
			description = description +
				wxT(", ") + record->GetName();
		}
	}
	return description;
}