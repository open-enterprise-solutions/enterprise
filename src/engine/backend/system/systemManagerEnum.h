#ifndef _SYSTEMOBJECTS_ENUMS_H__
#define _SYSTEMOBJECTS_ENUMS_H__

#include "systemEnum.h"
#include "backend/compiler/enumUnit.h"

class ibValueEnumStatusMessage : public ibValueEnumeration<ibStatusMessage> {
	public:
	ibValueEnumStatusMessage() : ibValueEnumeration() {}
	//ibValueEnumStatusMessage(ibStatusMessage status) : ibValueEnumeration(status) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibStatusMessage::ibStatusMessage_Information, wxT("Information"), _("Information"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Warning, wxT("Warning"), _("Warning"));
		AddEnumeration(ibStatusMessage::ibStatusMessage_Error, wxT("Error"), _("Error"));
	}
};

class ibValueEnumQuestionMode : public ibValueEnumeration<ibQuestionMode> {
	public:
	ibValueEnumQuestionMode() : ibValueEnumeration() {}
	//ibValueEnumQuestionMode(ibQuestionMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNo, wxT("YesNo"), _("Yes or no"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_YesNoCancel, wxT("YesNoCancel"), _("Yes or no or cancel"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OK, wxT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionMode::ibQuestionMode_OKCancel, wxT("OkCancel"), _("Ok or cancel"));
	}
};

class ibValueEnumQuestionReturnCode : public ibValueEnumeration<ibQuestionReturnCode> {
	public:
	ibValueEnumQuestionReturnCode() : ibValueEnumeration() {}
	//ibValueEnumQuestionReturnCode(ibQuestionReturnCode code) : ibValueEnumeration(code) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes, wxT("Yes"), _("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_No, wxT("No"), _("Yes"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_OK, wxT("Ok"), _("Ok"));
		AddEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Cancel, wxT("Cancel"), _("Cancel"));
	}
};

class ibValueEnumRoundMode : public ibValueEnumeration<ibRoundMode> {
	public:
	ibValueEnumRoundMode() : ibValueEnumeration() {}
	//ibValueEnumRoundMode(ibRoundMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as10, wxT("Round15as10"), _("Round 15 as 10"));
		AddEnumeration(ibRoundMode::ibRoundMode_Round15as20, wxT("Round15as20"), _("Round 15 as 20"));
	}
};

class ibValueChars : public ibValueEnumeration<ibChars> {
	public:
	ibValueChars() : ibValueEnumeration() {}
	//ibValueChars(ibChars c) : ibValueEnumeration(c) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibChars::eCR, wxT("CR"));
		AddEnumeration(ibChars::eFF, wxT("FF"));
		AddEnumeration(ibChars::eLF, wxT("LF"));
		AddEnumeration(ibChars::eNBSp, wxT("NBSp"));
		AddEnumeration(ibChars::eTab, wxT("Tab"));
		AddEnumeration(ibChars::eVTab, wxT("VTab"));
	}

	virtual wxString GetDescription(ibChars val) const {
		return (char)val;
	}
};

#endif