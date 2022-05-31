#include "sizers.h"

void CValueBoxSizer::SetPropertyData(Property *property, const CValue &srcValue)
{
	IValueFrame::SetPropertyData(property, srcValue);
}

#include "frontend/visualView/special/enums/valueOrient.h"

CValue CValueBoxSizer::GetPropertyData(Property *property)
{
	if (property->GetName() == wxT("orient"))
	{
		return new CValueEnumOrient(m_orient);
	}

	return IValueFrame::GetPropertyData(property);
}