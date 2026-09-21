#ifndef _SYSTEM_ENUMS_H__
#define _SYSTEM_ENUMS_H__

enum ibStatusMessage
{
	ibStatusMessage_Information = 1,
	ibStatusMessage_Warning,
	ibStatusMessage_Error
};

enum ibQuestionMode
{
	ibQuestionMode_YesNo = 1,
	ibQuestionMode_YesNoCancel,
	ibQuestionMode_OK,
	ibQuestionMode_OKCancel
};

enum ibQuestionReturnCode
{
	ibQuestionReturnCode_Yes = 1,
	ibQuestionReturnCode_No,
	ibQuestionReturnCode_OK,
	ibQuestionReturnCode_Cancel
};

enum ibRoundMode
{
	ibRoundMode_Round15as10 = 1,
	ibRoundMode_Round15as20
};

// How the TEXT of a file is spelled in bytes - what a TextReader decodes by and a TextWriter encodes by.
// ANSI and OEM are the two code pages Windows keeps per system; elsewhere both mean the locale's own.
enum ibTextEncoding
{
	ibTextEncoding_UTF8 = 1,
	ibTextEncoding_UTF16,
	ibTextEncoding_ANSI,
	ibTextEncoding_OEM,
	ibTextEncoding_System
};

enum ibChars {
	eCR = 13,
	eFF = 12,
	eLF = 10,
	eNBSp = 160,
	eTab = 9,
	eVTab = 11,
};

// What a JSONReader stands on after a Read(): the kind of the current token. `None` is "nothing read yet" and
// "nothing left" - the two moments a reader has no token under it.
enum ibJsonValueType
{
	ibJsonValueType_None = 1,
	ibJsonValueType_Null,
	ibJsonValueType_Boolean,
	ibJsonValueType_Number,
	ibJsonValueType_String,
	ibJsonValueType_PropertyName,
	ibJsonValueType_ObjectStart,
	ibJsonValueType_ObjectEnd,
	ibJsonValueType_ArrayStart,
	ibJsonValueType_ArrayEnd
};

// How a JSONWriter lays its text out: in one line, or a member to a line with tab indents.
enum ibJsonFormatting
{
	ibJsonFormatting_Compact = 1,
	ibJsonFormatting_Indented
};

#endif