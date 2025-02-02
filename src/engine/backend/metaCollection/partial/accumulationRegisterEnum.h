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

#include "backend/compiler/enumObject.h"

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

protected:

	void CreateEnumeration(){
		AddEnumeration(eExpense, wxT("expense"));
		AddEnumeration(eReceipt, wxT("receipt"));
	}

	virtual void InitializeEnumeration() override {
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eRecordType value) override {
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

const class_identifier_t g_enumRecordTypeCLSID = string_to_clsid("EN_RETP");

#endif