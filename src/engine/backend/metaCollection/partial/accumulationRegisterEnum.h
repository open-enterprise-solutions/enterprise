#ifndef _ACCUMULATION_REGISTER_ENUM_H__
#define _ACCUMULATION_REGISTER_ENUM_H__

enum ibRegisterType {
	eBalances,
	eTurnovers
};

enum ibRecordType {
	eExpense,
	eReceipt
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumAccumulationRegisterType : public ibValueEnumeration<ibRegisterType> {
	public:
	ibValueEnumAccumulationRegisterType() : ibValueEnumeration() {}
	//ibValueEnumAccumulationRegisterType(ibRegisterType mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRegisterType::eBalances, ibT("Balances"), ibTRANSLATE("Balances"));
		AddEnumeration(ibRegisterType::eTurnovers, ibT("Turnovers"), ibTRANSLATE("Turnovers"));
	}
};
class ibValueEnumAccumulationRegisterRecordType : public ibValueEnumeration<ibRecordType> {
	public:
	// A new record is a receipt unless the code says otherwise (#411).
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumAccumulationRegisterRecordType>(ibRecordType::eReceipt);
	}

	ibValueEnumAccumulationRegisterRecordType() : ibValueEnumeration() {}
	//ibValueEnumAccumulationRegisterRecordType(ibRecordType recordType) : ibValueEnumeration(recordType) {}

	virtual void CreateEnumeration() {
		AddEnumeration(eExpense, ibT("Expense"), ibTRANSLATE("Expense"));
		AddEnumeration(eReceipt, ibT("Receipt"), ibTRANSLATE("Receipt"));
	}
};
constexpr ibClassID g_enumRecordTypeCLSID = enum_to_clsid("EN_RETP");
#pragma endregion 

#endif
