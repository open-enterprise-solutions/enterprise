#ifndef _ACCOUNTING_REGISTER_ENUM_H__
#define _ACCOUNTING_REGISTER_ENUM_H__

enum ibAccountingRecordType {
	eDebit,
	eCredit
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumAccountingRegisterRecordType : public ibValueEnumeration<ibAccountingRecordType> {
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumAccountingRegisterRecordType);
public:
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(ibAccountingRecordType::eDebit);
	}

	ibValueEnumAccountingRegisterRecordType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(eDebit, wxT("Debit"), _("Debit"));
		AddEnumeration(eCredit, wxT("Credit"), _("Credit"));
	}
};
const ibClassID g_enumAccountingRecordTypeCLSID = string_to_clsid("EN_ARTP");
#pragma endregion

#endif
