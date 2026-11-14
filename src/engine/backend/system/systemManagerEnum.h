#ifndef _SYSTEMOBJECTS_ENUMS_H__
#define _SYSTEMOBJECTS_ENUMS_H__

#include "systemEnum.h"
#include "backend/compiler/enumUnit.h"
#include "core/xml/xmlNodeType.h"   // ibXmlNodeType - the parser's; the enumeration below is a script's view of it

class ibValueEnumStatusMessage : public ibValueEnumeration<ibStatusMessage> {
	public:
	ibValueEnumStatusMessage() : ibValueEnumeration() {}
	//ibValueEnumStatusMessage(ibStatusMessage status) : ibValueEnumeration(status) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibStatusMessage::ibStatusMessage_Information, ibT("Information"), ibTRANSLATE("Information"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Warning, ibT("Warning"), ibTRANSLATE("Warning"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Error, ibT("Error"), ibTRANSLATE("Error"));
	}
};

class ibValueEnumQuestionMode : public ibValueEnumeration<ibQuestionMode> {
	public:
	ibValueEnumQuestionMode() : ibValueEnumeration() {}
	//ibValueEnumQuestionMode(ibQuestionMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNo, ibT("YesNo"), ibTRANSLATE("Yes or no"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNoCancel, ibT("YesNoCancel"), ibTRANSLATE("Yes or no or cancel"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OK, ibT("Ok"), ibTRANSLATE("Ok"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OKCancel, ibT("OkCancel"), ibTRANSLATE("Ok or cancel"));
	}
};

class ibValueEnumQuestionReturnCode : public ibValueEnumeration<ibQuestionReturnCode> {
	public:
	ibValueEnumQuestionReturnCode() : ibValueEnumeration() {}
	//ibValueEnumQuestionReturnCode(ibQuestionReturnCode code) : ibValueEnumeration(code) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes, ibT("Yes"), ibTRANSLATE("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_No, ibT("No"), ibTRANSLATE("No"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_OK, ibT("Ok"), ibTRANSLATE("Ok"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Cancel, ibT("Cancel"), ibTRANSLATE("Cancel"));
	}
};

class ibValueEnumRoundMode : public ibValueEnumeration<ibRoundMode> {
	public:
	ibValueEnumRoundMode() : ibValueEnumeration() {}
	//ibValueEnumRoundMode(ibRoundMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as10, ibT("Round15as10"), ibTRANSLATE("Round 15 as 10"));
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as20, ibT("Round15as20"), ibTRANSLATE("Round 15 as 20"));
	}
};

class ibValueEnumTextEncoding : public ibValueEnumeration<ibTextEncoding> {
	public:
	ibValueEnumTextEncoding() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibTextEncoding::ibTextEncoding_UTF8, ibT("UTF8"), ibTRANSLATE("UTF-8"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_UTF16, ibT("UTF16"), ibTRANSLATE("UTF-16"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_ANSI, ibT("ANSI"), ibTRANSLATE("ANSI code page of the system"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_OEM, ibT("OEM"), ibTRANSLATE("OEM code page of the system"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_System, ibT("System"), ibTRANSLATE("Encoding of the current locale"));
	}
};

class ibValueEnumHashFunction : public ibValueEnumeration<ibHashFunction> {
	public:
	ibValueEnumHashFunction() : ibValueEnumeration() {}

	// A function is the standard's name, the same in every language - not a catalog entry.
	virtual void CreateEnumeration() {
		AddEnumeration(ibHashFunction::ibHashFunction_MD5, ibT("MD5"), ibT("MD5"));
		AddEnumeration(ibHashFunction::ibHashFunction_SHA1, ibT("SHA1"), ibT("SHA-1"));
		AddEnumeration(ibHashFunction::ibHashFunction_SHA256, ibT("SHA256"), ibT("SHA-256"));
		AddEnumeration(ibHashFunction::ibHashFunction_SHA384, ibT("SHA384"), ibT("SHA-384"));
		AddEnumeration(ibHashFunction::ibHashFunction_SHA512, ibT("SHA512"), ibT("SHA-512"));
	}
};

class ibValueEnumHttpMethod : public ibValueEnumeration<ibHttpMethod> {
	public:
	ibValueEnumHttpMethod() : ibValueEnumeration() {}

	// A method is the protocol's word, the same in every language - not a catalog entry, where "Post" and
	// "Delete" are a document's and a list's commands and translate as actions. So it has NO description: a
	// description is a msgid, translated where it is read, and a member without one shows its name as it is.
	virtual void CreateEnumeration() {
		AddEnumeration(ibHttpMethod::ibHttpMethod_Get, ibT("Get"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Post, ibT("Post"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Put, ibT("Put"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Patch, ibT("Patch"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Delete, ibT("Delete"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Head, ibT("Head"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Options, ibT("Options"));
	}
};

class ibValueChars : public ibValueEnumeration<ibChars> {
	public:
	ibValueChars() : ibValueEnumeration() {}
	//ibValueChars(ibChars c) : ibValueEnumeration(c) {}

	// A CHARACTER READS AS ITSELF: `"a" + Chars.LF` is a line break, not the word "LF". What a member
	// shows is its description (ibValueEnumerationVariant::GetString), so the character is the
	// description. (It used to live in a GetDescription that overrode nothing and was never called.)
	virtual void CreateEnumeration() {
		AddChar(ibChars::eCR, ibT("CR"));
		AddChar(ibChars::eFF, ibT("FF"));
		AddChar(ibChars::eLF, ibT("LF"));
		AddChar(ibChars::eNBSp, ibT("NBSp"));
		AddChar(ibChars::eTab, ibT("Tab"));
		AddChar(ibChars::eVTab, ibT("VTab"));
	}

private:
	void AddChar(ibChars c, const ibString& name) {
		AddEnumeration(c, name, ibString(static_cast<ibChar>(c)));
	}
};

class ibValueEnumJsonValueType : public ibValueEnumeration<ibJsonValueType> {
	public:
	ibValueEnumJsonValueType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibJsonValueType::ibJsonValueType_None, ibT("None"), ibTRANSLATE("None"));
		// `JSONValueType.Null` is writable since the lexer takes a constant's word after a dot as a member name
		// (translateCode.cpp) - before that, a member of this name was one nobody could spell.
		AddEnumeration(ibJsonValueType::ibJsonValueType_Null, ibT("Null"), ibTRANSLATE("Null"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_Boolean, ibT("Boolean"), ibTRANSLATE("Boolean"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_Number, ibT("Number"), ibTRANSLATE("Number"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_String, ibT("String"), ibTRANSLATE("String"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_PropertyName, ibT("PropertyName"), ibTRANSLATE("Property name"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ObjectStart, ibT("ObjectStart"), ibTRANSLATE("Object start"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ObjectEnd, ibT("ObjectEnd"), ibTRANSLATE("Object end"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ArrayStart, ibT("ArrayStart"), ibTRANSLATE("Array start"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ArrayEnd, ibT("ArrayEnd"), ibTRANSLATE("Array end"));
	}
};

class ibValueEnumJsonFormatting : public ibValueEnumeration<ibJsonFormatting> {
	public:
	ibValueEnumJsonFormatting() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibJsonFormatting::ibJsonFormatting_Compact, ibT("Compact"), ibTRANSLATE("Compact"));
		AddEnumeration(ibJsonFormatting::ibJsonFormatting_Indented, ibT("Indented"), ibTRANSLATE("Indented"));
	}
};

class ibValueEnumXmlNodeType : public ibValueEnumeration<ibXmlNodeType> {
	public:
	ibValueEnumXmlNodeType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_None, ibT("None"), ibTRANSLATE("None"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_StartElement, ibT("StartElement"), ibTRANSLATE("Start element"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EndElement, ibT("EndElement"), ibTRANSLATE("End element"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Text, ibT("Text"), ibTRANSLATE("Text"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Whitespace, ibT("Whitespace"), ibTRANSLATE("Whitespace"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_CDATASection, ibT("CDATASection"), ibTRANSLATE("CDATA section"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Comment, ibT("Comment"), ibTRANSLATE("Comment"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_ProcessingInstruction, ibT("ProcessingInstruction"), ibTRANSLATE("Processing instruction"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_XMLDeclaration, ibT("XMLDeclaration"), ibTRANSLATE("XML declaration"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_DocumentTypeDefinition, ibT("DocumentTypeDefinition"), ibTRANSLATE("Document type definition"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Attribute, ibT("Attribute"), ibTRANSLATE("Attribute"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EntityReference, ibT("EntityReference"), ibTRANSLATE("Entity reference"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Entity, ibT("Entity"), ibTRANSLATE("Entity"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EndEntity, ibT("EndEntity"), ibTRANSLATE("End entity"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Notation, ibT("Notation"), ibTRANSLATE("Notation"));
	}
};

class ibValueEnumXmlSpace : public ibValueEnumeration<ibXmlSpace> {
	public:
	ibValueEnumXmlSpace() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibXmlSpace::ibXmlSpace_Default, ibT("Default"), ibTRANSLATE("Default"));
		AddEnumeration(ibXmlSpace::ibXmlSpace_Preserve, ibT("Preserve"), ibTRANSLATE("Preserve"));
	}
};

class ibValueEnumBackgroundJobState : public ibValueEnumeration<ibBackgroundJobState> {
	public:
	ibValueEnumBackgroundJobState() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibBackgroundJobState::ibBackgroundJobState_Active, ibT("Active"), ibTRANSLATE("Active"));
		AddEnumeration(ibBackgroundJobState::ibBackgroundJobState_Completed, ibT("Completed"), ibTRANSLATE("Completed"));
		AddEnumeration(ibBackgroundJobState::ibBackgroundJobState_Failed, ibT("Failed"), ibTRANSLATE("Failed"));
		AddEnumeration(ibBackgroundJobState::ibBackgroundJobState_Cancelled, ibT("Cancelled"), ibTRANSLATE("Cancelled"));
	}
};

#endif