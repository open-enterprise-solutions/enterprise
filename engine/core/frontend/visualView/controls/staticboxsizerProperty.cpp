#include "sizers.h"

void CValueStaticBoxSizer::SetPropertyData(Property *property, const CValue &srcValue)
{
	IValueFrame::SetPropertyData(property, srcValue);
}

#include "frontend/visualView/special/enums/valueOrient.h"

CValue CValueStaticBoxSizer::GetPropertyData(Property *property)
{
	if (property->GetName() == wxT("orient")) {
		return new CValueEnumOrient(m_orient);
	}

	return IValueFrame::GetPropertyData(property);
}