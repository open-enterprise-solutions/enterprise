#ifndef _SYSTEMOBJECTS_ENUMS_H__
#define _SYSTEMOBJECTS_ENUMS_H__

#include "systemEnums.h"
#include "compiler/enumObject.h"

class CValueStatusMessage : public IEnumeration<eStatusMessage>
{
	wxDECLARE_DYNAMIC_CLASS(CValueStatusMessage);

public:

	CValueStatusMessage() : IEnumeration() { InitializeEnumeration(); }
	CValueStatusMessage(eStatusMessage status) : IEnumeration(status) { InitializeEnumeration(status); }

	wxString GetTypeString() const override { return wxT("statusMessage"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eStatusMessage::eStatusMessage_Information, wxT("information"));
		AddEnumeration(eStatusMessage::eStatusMessage_Warning, wxT("warning"));
		AddEnumeration(eStatusMessage::eStatusMessage_Error, wxT("error"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eStatusMessage value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueQuestionMode : public IEnumeration<eQuestionMode>
{
	wxDECLARE_DYNAMIC_CLASS(CValueQuestionMode);

public:

	CValueQuestionMode() : IEnumeration() { InitializeEnumeration(); }
	CValueQuestionMode(eQuestionMode mode) : IEnumeration(mode) { InitializeEnumeration(mode); }

	wxString GetTypeString() const override { return wxT("questionMode"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eQuestionMode::eQuestionMode_YesNo, wxT("yesNo"));
		AddEnumeration(eQuestionMode::eQuestionMode_YesNoCancel, wxT("yesNoCancel"));
		AddEnumeration(eQuestionMode::eQuestionMode_OK, wxT("ok"));
		AddEnumeration(eQuestionMode::eQuestionMode_OKCancel, wxT("okCancel"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eQuestionMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueQuestionReturnCode : public IEnumeration<eQuestionReturnCode>
{
	wxDECLARE_DYNAMIC_CLASS(CValueQuestionReturnCode);

public:

	CValueQuestionReturnCode() : IEnumeration() { InitializeEnumeration(); }
	CValueQuestionReturnCode(eQuestionReturnCode code) : IEnumeration(code) { InitializeEnumeration(code); }

	wxString GetTypeString() const override { return wxT("questionReturnCode"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eQuestionReturnCode::eQuestionReturnCode_Yes, wxT("yes"));
		AddEnumeration(eQuestionReturnCode::eQuestionReturnCode_No, wxT("no"));
		AddEnumeration(eQuestionReturnCode::eQuestionReturnCode_OK, wxT("ok"));
		AddEnumeration(eQuestionReturnCode::eQuestionReturnCode_Cancel, wxT("cancel"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eQuestionReturnCode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueRoundMode : public IEnumeration<eRoundMode>
{
	wxDECLARE_DYNAMIC_CLASS(CValueQuestionReturnCode);

public:

	CValueRoundMode() : IEnumeration() { InitializeEnumeration(); }
	CValueRoundMode(eRoundMode mode) : IEnumeration(mode) { InitializeEnumeration(mode); }

	wxString GetTypeString() const override { return wxT("roundMode"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eRoundMode::eRoundMode_Round15as10, wxT("round15as10"));
		AddEnumeration(eRoundMode::eRoundMode_Round15as20, wxT("round15as20"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eRoundMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueChars : public IEnumeration<enChars>
{
	wxDECLARE_DYNAMIC_CLASS(CValueChars);

public:

	CValueChars() : IEnumeration() { InitializeEnumeration(); }
	CValueChars(enChars c) : IEnumeration(c) { InitializeEnumeration(c); }

	wxString GetTypeString() const override { return wxT("chars"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(enChars::eCR, wxT("CR"));
		AddEnumeration(enChars::eFF, wxT("FF"));
		AddEnumeration(enChars::eLF, wxT("LF"));
		AddEnumeration(enChars::eNBSp, wxT("NBSp"));
		AddEnumeration(enChars::eTab, wxT("Tab"));
		AddEnumeration(enChars::eVTab, wxT("VTab"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(enChars c) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(c);
	}

	virtual wxString GetDescription(enChars val) const {
		return (char)val;
	}
};

#endif