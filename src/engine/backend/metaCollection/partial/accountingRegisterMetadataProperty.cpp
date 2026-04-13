#include "accountingRegister.h"
#include "backend/metadata.h"
#include "backend/objCtor.h"

void ibValueMetaObjectAccountingRegister::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// When Chart of Accounts binding changes, update the Account field type
	if (m_propertyChartOfAccounts == property) {
		const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();
		ibTypeDescription typeDesc;
		for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
			const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
			if (chartOfAccounts != nullptr) {
				const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(chartOfAccounts, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				wxASSERT(so);
				typeDesc.AppendMetaType(so->GetClassType());
			}
		}
		(*m_propertyAttributeAccount)->SetDefaultMetaType(typeDesc);
	}

	// Enable/disable Account field based on binding
	if ((*m_propertyAttributeAccount)->GetClsidCount() > 0)
		(*m_propertyAttributeAccount)->ClearFlag(metaDisableFlag);
	else
		(*m_propertyAttributeAccount)->SetFlag(metaDisableFlag);

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}
