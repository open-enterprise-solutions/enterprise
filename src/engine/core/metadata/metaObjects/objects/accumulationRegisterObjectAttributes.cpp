////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister object - attributes
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "compiler/methods.h"
#include "reference/reference.h"
#include "utils/stringUtils.h"

enum {
	enThisObject,
	enFilter
};

void CRecordSetAccumulationRegister::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
}

CValue CRecordSetAccumulationRegister::GetAttribute(attributeArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enThisObject:
		return this;
	case enFilter:
		return m_recordSetKeyValue;
	}

	return CValue();
}