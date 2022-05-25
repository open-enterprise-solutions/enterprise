#include "accumulationRegister.h"

void CMetaObjectAccumulationRegister::OnPropertyChanged(Property* property)
{
	if (m_registerType == eRegisterType::eBalances) {
		m_attributeRecordType->ClearFlag(metaDisableObjectFlag);
	}
	else if (m_registerType == eRegisterType::eTurnovers) {
		m_attributeRecordType->SetFlag(metaDisableObjectFlag);
	}

	IMetaObjectRegisterData::OnPropertyChanged(property);
}