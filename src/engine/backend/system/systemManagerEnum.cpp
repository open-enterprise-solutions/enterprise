////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManagerEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValuibStatusMessage, "StatusMessage", string_to_clsid("EN_STMS"));
ENUM_TYPE_REGISTER(ibValuibQuestionMode, "QuestionMode", string_to_clsid("EN_QSMD"));
ENUM_TYPE_REGISTER(ibValuibQuestionReturnCode, "QuestionReturnCode", string_to_clsid("EN_QSRC"));
ENUM_TYPE_REGISTER(ibValuibRoundMode, "RoundMode", string_to_clsid("EN_ROMO"));

ENUM_TYPE_REGISTER(ibValueChars, "Chars", string_to_clsid("EN_CHAR"));