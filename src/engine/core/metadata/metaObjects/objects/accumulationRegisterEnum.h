#ifndef _ACCUMULATION_REGISTER_ENUM_H__
#define _ACCUMULATION_REGISTER_ENUM_H__

enum eRegisterType {
	eBalances,
	eTurnovers
};

enum eRecordType {
	eExpense,
	eReceipt
};

#include "compiler/enumObject.h"

class CValueEnumAccumulationRegisterRecordType : public IEnumeration<eRecordType> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumAccumulationRegisterRecordType);
public:

	static CValue CreateDefEnumValue();

	CValueEnumAccumulationRegisterRecordType() : IEnumeration() {
		InitializeEnumeration();
	}

	CValueEnumAccumulationRegisterRecordType(eRecordType recordType) : IEnumeration(recordType) {
		InitializeEnumeration(recordType);
	}

	virtual wxString GetTypeString() const override {
		return wxT("accumulationRecordType");
	}

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eExpense, wxT("expense"));
		AddEnumeration(eReceipt, wxT("receipt"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eRecordType value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

const CLASS_ID g_enumRecordTypeCLSID = TEXT2CLSID("EN_RETP");

#endif