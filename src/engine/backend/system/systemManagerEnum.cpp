////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManagerEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumStatusMessage, "StatusMessage", enum_to_clsid("EN_STMS"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionMode, "QuestionMode", enum_to_clsid("EN_QSMD"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionReturnCode, "QuestionReturnCode", enum_to_clsid("EN_QSRC"));
ENUM_TYPE_REGISTER(ibValueEnumRoundMode, "RoundMode", enum_to_clsid("EN_ROMO"));

ENUM_TYPE_REGISTER(ibValueChars, "Chars", enum_to_clsid("EN_CHAR"));
