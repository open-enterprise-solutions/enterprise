#include "accountingRegister.h"

void ibValueMetaObjectAccountingRegister::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}
