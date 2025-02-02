////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManagerEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStatusMessage, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQuestionMode, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQuestionReturnCode, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueRoundMode, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueChars, CValue);

//add new enumeration
ENUM_TYPE_REGISTER(CValueStatusMessage, "statusMessage", string_to_clsid("EN_STMS"));
ENUM_TYPE_REGISTER(CValueQuestionMode, "questionMode", string_to_clsid("EN_QSMD"));
ENUM_TYPE_REGISTER(CValueQuestionReturnCode, "questionReturnCode", string_to_clsid("EN_QSRC"));
ENUM_TYPE_REGISTER(CValueRoundMode, "roundMode", string_to_clsid("EN_ROMO"));

ENUM_TYPE_REGISTER(CValueChars, "chars", string_to_clsid("EN_CHAR"));