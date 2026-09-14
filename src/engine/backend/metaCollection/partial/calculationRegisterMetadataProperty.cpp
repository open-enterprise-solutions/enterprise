#include "calculationRegister.h"

void ibValueMetaObjectCalculationRegister::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// ALWAYS subordinate to a recorder — the recorder-family attributes are always active.
	(*m_propertyAttributeLineActive)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributeRecorder)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributeLineNumber)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributePeriod)->ClearFlag(metaDisableFlag);

	// The chart binding is a TYPE, and the type has to follow the property the moment it is chosen —
	// otherwise the designer picks a chart, sees the property filled, and the CalculationType column
	// is still untyped underneath. ApplyChartBinding is the one place that knows how a chart becomes
	// a type; load and reload reach it too.
	if (property == m_propertyChartOfCalculationTypes)
		ApplyChartBinding();

	// Recalculation switched on in a register made before its marks had a holder: the holder takes its number now — an
	// edit, like any, so the next apply meets a table it has not got and creates it.
	if (property == m_propertyUseRecalculation && IsUseRecalculation() && m_recalculation->GetMetaID() == 0)
		m_recalculation->OnCreateMetaObject(m_metaData, newObjectFlag);

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}
