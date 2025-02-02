#include "documentVariant.h"

wxString wxVariantRecordData::MakeString() const
{
	wxString description;
	for (auto record_id : m_recordData) {
		IMetaObjectRegisterData* record =
			dynamic_cast<IMetaObjectRegisterData*>(m_metaData->GetMetaObject(record_id));
		if (record == nullptr || !record->IsAllowed())
			continue;
		if (!record->HasRecorder())
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