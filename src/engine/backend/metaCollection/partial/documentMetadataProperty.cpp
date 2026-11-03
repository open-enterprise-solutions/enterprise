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

bool ibValueMetaObjectDocument::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectDocument::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// ⭐ EITHER LIST SAYS THE SAME THING TO WHAT IT NAMES: "this document writes here". The registers
	// it posts movements to and the sequences it registers in are two lists (they are two acts), and
	// what happens when one of them changes is one act — each named table's Recorder learns or
	// forgets this document's reference. Written once for both, or a sequence declared on a running
	// configuration would refuse to save: "no recorder" (2026-09-18).
	//
	// ⭐ BY WHAT THE LISTS SAY NOW, not by what changed between two values. The inspector writes a list IN PLACE
	// (SetNodeValue, propertyObject.cpp: "a cell read in place"), so the value before the change IS the value after
	// it, and the difference of the two was empty: a sequence bound from the inspector never learned this document
	// and refused to save — "no recorder" (2026-11-03). Every register and sequence is told by BOTH lists at once:
	// a register named in one must not lose this document because the other one changed.
	if (m_propertyRegisterRecord == property || m_propertySequenceRecord == property) {
		const ibMetaDescription& movements = GetRecordDescription(ibRecorderWrites::Movements);
		const ibMetaDescription& sequences = GetRecordDescription(ibRecorderWrites::Sequences);
		for (ibValueMetaObjectRegisterData* registerData : m_metaData->GetAnyArrayObject<ibValueMetaObjectRegisterData>()) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			if (movements.ContainMetaType(registerData->GetMetaID()) || sequences.ContainMetaType(registerData->GetMetaID()))
				infoRecorder->GetTypeDesc().AppendMetaType((*m_propertyAttributeReference)->GetTypeDesc());
			else
				infoRecorder->GetTypeDesc().ClearMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	if (ibValueMetaObjectDocument::OnReloadMetaObject()) ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
}
