#include "accumulationRegister.h"

void CMetaObjectAccumulationRegister::OnPropertyChanged(Property* property)
{
	if (GetRegisterType() == eRegisterType::eBalances) {
		m_attributeRecordType->ClearFlag(metaDisableObjectFlag);
	}
	else if (GetRegisterType() == eRegisterType::eTurnovers) {
		m_attributeRecordType->SetFlag(metaDisableObjectFlag);
	}

	IMetaObjectRegisterData::OnPropertyChanged(property);
}