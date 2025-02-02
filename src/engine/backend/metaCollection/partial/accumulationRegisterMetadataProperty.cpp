#include "accumulationRegister.h"

void CMetaObjectAccumulationRegister::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (GetRegisterType() == eRegisterType::eBalances) {
		m_attributeRecordType->ClearFlag(metaDisableFlag);
	}
	else if (GetRegisterType() == eRegisterType::eTurnovers) {
		m_attributeRecordType->SetFlag(metaDisableFlag);
	}

	IMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}