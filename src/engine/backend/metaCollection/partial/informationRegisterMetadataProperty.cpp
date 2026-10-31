#include "informationRegister.h"

void ibValueMetaObjectInformationRegister::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		(*m_propertyAttributeLineActive)->ClearFlag(metaDisableFlag);
		(*m_propertyAttributeRecorder)->ClearFlag(metaDisableFlag);
		(*m_propertyAttributeLineNumber)->ClearFlag(metaDisableFlag);
	}
	else if (GetWriteRegisterMode() == ibWriteRegisterMode::eIndependent) {
		(*m_propertyAttributeLineActive)->SetFlag(metaDisableFlag);
		(*m_propertyAttributeRecorder)->SetFlag(metaDisableFlag);
		(*m_propertyAttributeLineNumber)->SetFlag(metaDisableFlag);
	}

	if (GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		(*m_propertyAttributePeriod)->ClearFlag(metaDisableFlag);
	}
	else {
		(*m_propertyAttributePeriod)->SetFlag(metaDisableFlag);
	}

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}