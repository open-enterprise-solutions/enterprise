#ifndef _SYSTEM_ENUMS_H__
#define _SYSTEM_ENUMS_H__

enum eStatusMessage
{
	eStatusMessage_Information = 1,
	eStatusMessage_Warning,
	eStatusMessage_Error
};

enum eQuestionMode
{
	eQuestionMode_YesNo = 1,
	eQuestionMode_YesNoCancel,
	eQuestionMode_OK,
	eQuestionMode_OKCancel
};

enum eQuestionReturnCode
{
	eQuestionReturnCode_Yes = 1,
	eQuestionReturnCode_No,
	eQuestionReturnCode_OK,
	eQuestionReturnCode_Cancel
};

enum eRoundMode
{
	eRoundMode_Round15as10 = 1,
	eRoundMode_Round15as20
};

enum enChars {
	eCR = 13,
	eFF = 12,
	eLF = 10,
	eNBSp = 160,
	eTab = 9,
	eVTab = 11,
};

#endif