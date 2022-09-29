#include "informationRegister.h"

void CMetaObjectInformationRegister::OnPropertyChanged(Property* property)
{
	if (GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		m_attributeLineActive->ClearFlag(metaDisableObjectFlag);
		m_attributeRecorder->ClearFlag(metaDisableObjectFlag);
		m_attributeLineNumber->ClearFlag(metaDisableObjectFlag);
	}
	else if (GetWriteRegisterMode() == eWriteRegisterMode::eIndependent) {
		m_attributeLineActive->SetFlag(metaDisableObjectFlag);
		m_attributeRecorder->SetFlag(metaDisableObjectFlag);
		m_attributeLineNumber->SetFlag(metaDisableObjectFlag);
	}

	if (GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		m_attributePeriod->ClearFlag(metaDisableObjectFlag);
	}
	else {
		m_attributePeriod->SetFlag(metaDisableObjectFlag);
	}

	IMetaObjectRegisterData::OnPropertyChanged(property);
}