#include "informationRegister.h"

void CMetaObjectInformationRegister::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		m_attributeLineActive->ClearFlag(metaDisableFlag);
		m_attributeRecorder->ClearFlag(metaDisableFlag);
		m_attributeLineNumber->ClearFlag(metaDisableFlag);
	}
	else if (GetWriteRegisterMode() == eWriteRegisterMode::eIndependent) {
		m_attributeLineActive->SetFlag(metaDisableFlag);
		m_attributeRecorder->SetFlag(metaDisableFlag);
		m_attributeLineNumber->SetFlag(metaDisableFlag);
	}

	if (GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		m_attributePeriod->ClearFlag(metaDisableFlag);
	}
	else {
		m_attributePeriod->SetFlag(metaDisableFlag);
	}

	IMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}