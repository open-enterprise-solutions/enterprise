#include "accumulationRegister.h"

void ibValueMetaObjectAccumulationRegister::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (GetRegisterType() == ibRegisterType::eBalances) {
		(*m_propertyAttributeRecordType)->ClearFlag(metaDisableFlag);
	}
	else if (GetRegisterType() == ibRegisterType::eTurnovers) {
		(*m_propertyAttributeRecordType)->SetFlag(metaDisableFlag);
	}

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}