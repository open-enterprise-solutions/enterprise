#ifndef _CHART_OF_ACCOUNTS_ENUM_H__
#define _CHART_OF_ACCOUNTS_ENUM_H__

enum ibAccountType {
	eActive,
	ePassive,
	eActivePassive
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumAccountType : public ibValueEnumeration<ibAccountType> {
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumAccountType);
public:
	ibValueEnumAccountType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibAccountType::eActive, wxT("Active"), _("Active"));
		AddEnumeration(ibAccountType::ePassive, wxT("Passive"), _("Passive"));
		AddEnumeration(ibAccountType::eActivePassive, wxT("ActivePassive"), _("Active/Passive"));
	}
};
const ibClassID g_enumAccountTypeCLSID = string_to_clsid("EN_ACTP");
#pragma endregion

#endif
