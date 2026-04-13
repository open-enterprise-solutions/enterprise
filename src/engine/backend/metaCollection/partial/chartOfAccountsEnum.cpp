////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts - enum
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccountsEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumAccountType, ibValue);

ENUM_TYPE_REGISTER(ibValueEnumAccountType, "AccountType", string_to_clsid("EN_ACTP"));
