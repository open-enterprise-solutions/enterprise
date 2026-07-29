////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectDocument::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectDocument::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectDocument::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (m_propertyRegisterRecord == property) {
		const ibMetaDescription& old_metaDesc = m_propertyRegisterRecord->GetValueAsMetaDesc(oldValue);
		const ibMetaDescription& new_metaDesc = m_propertyRegisterRecord->GetValueAsMetaDesc(newValue);
		for (unsigned int idx = 0; idx < old_metaDesc.GetTypeCount(); idx++) {
			if (new_metaDesc.ContainMetaType(old_metaDesc.GetByIdx(idx))) continue;
			const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(old_metaDesc.GetByIdx(idx));
			if (registerData != nullptr) {
				ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
				wxASSERT(infoRecorder);
				infoRecorder->GetTypeDesc().ClearMetaType((*m_propertyAttributeReference)->GetTypeDesc());
			}
		}
		for (unsigned int idx = 0; idx < new_metaDesc.GetTypeCount(); idx++) {
			if (old_metaDesc.ContainMetaType(new_metaDesc.GetByIdx(idx))) continue;
			ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(new_metaDesc.GetByIdx(idx));
			if (registerData != nullptr) {
				ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
				wxASSERT(infoRecorder);
				infoRecorder->GetTypeDesc().AppendMetaType((*m_propertyAttributeReference)->GetTypeDesc());
			}
		}
	}

	if (ibValueMetaObjectDocument::OnReloadMetaObject()) ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
}
