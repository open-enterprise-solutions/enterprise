////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register - enum
////////////////////////////////////////////////////////////////////////////

#include "accountingRegisterEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumAccountingRegisterRecordType, ibValue);

ENUM_TYPE_REGISTER(ibValueEnumAccountingRegisterRecordType, "AccountingRecordType", string_to_clsid("EN_ARTP"));
