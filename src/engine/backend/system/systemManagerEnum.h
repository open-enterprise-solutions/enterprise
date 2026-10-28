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
		AddEnumeration(ibStatusMessage::ibStatusMessage_Information, ibT("Information"), _("Information"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Warning, ibT("Warning"), _("Warning"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Error, ibT("Error"), _("Error"));
	}
};

class ibValueEnumQuestionMode : public ibValueEnumeration<ibQuestionMode> {
	public:
	ibValueEnumQuestionMode() : ibValueEnumeration() {}
	//ibValueEnumQuestionMode(ibQuestionMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNo, ibT("YesNo"), _("Yes or no"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNoCancel, ibT("YesNoCancel"), _("Yes or no or cancel"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OK, ibT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OKCancel, ibT("OkCancel"), _("Ok or cancel"));
	}
};

class ibValueEnumQuestionReturnCode : public ibValueEnumeration<ibQuestionReturnCode> {
	public:
	ibValueEnumQuestionReturnCode() : ibValueEnumeration() {}
	//ibValueEnumQuestionReturnCode(ibQuestionReturnCode code) : ibValueEnumeration(code) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes, ibT("Yes"), _("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_No, ibT("No"), _("No"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_OK, ibT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Cancel, ibT("Cancel"), _("Cancel"));
	}
};

class ibValueEnumRoundMode : public ibValueEnumeration<ibRoundMode> {
	public:
	ibValueEnumRoundMode() : ibValueEnumeration() {}
	//ibValueEnumRoundMode(ibRoundMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as10, ibT("Round15as10"), _("Round 15 as 10"));
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as20, ibT("Round15as20"), _("Round 15 as 20"));
	}
};

class ibValueEnumTextEncoding : public ibValueEnumeration<ibTextEncoding> {
	public:
	ibValueEnumTextEncoding() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibTextEncoding::ibTextEncoding_UTF8, ibT("UTF8"), _("UTF-8"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_UTF16, ibT("UTF16"), _("UTF-16"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_ANSI, ibT("ANSI"), _("ANSI code page of the system"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_OEM, ibT("OEM"), _("OEM code page of the system"));
		AddEnumeration(ibTextEncoding::ibTextEncoding_System, ibT("System"), _("Encoding of the current locale"));
	}
};

class ibValueEnumHttpMethod : public ibValueEnumeration<ibHttpMethod> {
	public:
	ibValueEnumHttpMethod() : ibValueEnumeration() {}

	// A method is the protocol's word, the same in every language - not a catalog entry, where "Post" and
	// "Delete" are a document's and a list's commands and translate as actions.
	virtual void CreateEnumeration() {
		AddEnumeration(ibHttpMethod::ibHttpMethod_Get, ibT("Get"), ibT("Get"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Post, ibT("Post"), ibT("Post"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Put, ibT("Put"), ibT("Put"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Patch, ibT("Patch"), ibT("Patch"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Delete, ibT("Delete"), ibT("Delete"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Head, ibT("Head"), ibT("Head"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Options, ibT("Options"), ibT("Options"));
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
		AddEnumeration(ibJsonValueType::ibJsonValueType_None, ibT("None"), _("None"));
		// `JSONValueType.Null` is writable since the lexer takes a constant's word after a dot as a member name
		// (translateCode.cpp) - before that, a member of this name was one nobody could spell.
		AddEnumeration(ibJsonValueType::ibJsonValueType_Null, ibT("Null"), _("Null"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_Boolean, ibT("Boolean"), _("Boolean"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_Number, ibT("Number"), _("Number"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_String, ibT("String"), _("String"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_PropertyName, ibT("PropertyName"), _("Property name"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ObjectStart, ibT("ObjectStart"), _("Object start"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ObjectEnd, ibT("ObjectEnd"), _("Object end"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ArrayStart, ibT("ArrayStart"), _("Array start"));
		AddEnumeration(ibJsonValueType::ibJsonValueType_ArrayEnd, ibT("ArrayEnd"), _("Array end"));
	}
};

class ibValueEnumJsonFormatting : public ibValueEnumeration<ibJsonFormatting> {
	public:
	ibValueEnumJsonFormatting() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibJsonFormatting::ibJsonFormatting_Compact, ibT("Compact"), _("Compact"));
		AddEnumeration(ibJsonFormatting::ibJsonFormatting_Indented, ibT("Indented"), _("Indented"));
	}
};

class ibValueEnumXmlNodeType : public ibValueEnumeration<ibXmlNodeType> {
	public:
	ibValueEnumXmlNodeType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_None, ibT("None"), _("None"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_StartElement, ibT("StartElement"), _("Start element"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EndElement, ibT("EndElement"), _("End element"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Text, ibT("Text"), _("Text"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Whitespace, ibT("Whitespace"), _("Whitespace"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_CDATASection, ibT("CDATASection"), _("CDATA section"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Comment, ibT("Comment"), _("Comment"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_ProcessingInstruction, ibT("ProcessingInstruction"), _("Processing instruction"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_XMLDeclaration, ibT("XMLDeclaration"), _("XML declaration"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_DocumentTypeDefinition, ibT("DocumentTypeDefinition"), _("Document type definition"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Attribute, ibT("Attribute"), _("Attribute"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EntityReference, ibT("EntityReference"), _("Entity reference"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Entity, ibT("Entity"), _("Entity"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_EndEntity, ibT("EndEntity"), _("End entity"));
		AddEnumeration(ibXmlNodeType::ibXmlNodeType_Notation, ibT("Notation"), _("Notation"));
	}
};

#endif