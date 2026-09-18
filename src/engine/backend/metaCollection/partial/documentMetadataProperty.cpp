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
	// ⭐ EITHER LIST SAYS THE SAME THING TO WHAT IT NAMES: "this document writes here". The registers
	// it posts movements to and the sequences it registers in are two lists (they are two acts), and
	// what happens when one of them changes is one act — each named table's Recorder learns or
	// forgets this document's reference. Written once for both, or a sequence declared on a running
	// configuration would refuse to save: "no recorder" (2026-09-18).
	ibPropertyRecord* named = nullptr;
	if (m_propertyRegisterRecord == property) named = m_propertyRegisterRecord;
	if (m_propertySequenceRecord == property) named = m_propertySequenceRecord;

	if (named != nullptr) {
		const ibMetaDescription& old_metaDesc = named->GetValueAsMetaDesc(oldValue);
		const ibMetaDescription& new_metaDesc = named->GetValueAsMetaDesc(newValue);
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
