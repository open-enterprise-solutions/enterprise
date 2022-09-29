#include "accumulationRegisterEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumAccumulationRegisterRecordType, CValue);

/////////////////////////////////////////////////////////////////////////

CValue CValueEnumAccumulationRegisterRecordType::CreateDefEnumValue()
{
	CValueEnumAccumulationRegisterRecordType *createdValue = new CValueEnumAccumulationRegisterRecordType(eRecordType::eExpense);
	if (createdValue->Init()) {
		CValue retValue = createdValue->GetEnumVariantValue();
		wxDELETE(createdValue);
		return retValue; 
	}
	return createdValue;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_REGISTER(CValueEnumAccumulationRegisterRecordType, "accumulationRecordType", g_enumRecordTypeCLSID);