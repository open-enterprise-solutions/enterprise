#include "accountingRegister.h"
#include "backend/metaData.h"
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

		// The dimension slots follow the binding too — a different chart declares a different
		// NUMBER of them. Re-typing only Account here was the old asymmetry: the slots kept the
		// previous chart's shape until the configuration happened to be run again.
		SyncAccountDimensionSlots();

		// ⭐ AND THE CREDIT ACCOUNT, WHICH IS A REFERENCE INTO THE SAME CHART. One declaration types
		// both sides — that is why there is no second chart binding — but only the debit half was
		// re-typed here, so on a correspondence register the credit account kept an empty type until
		// a run happened to fix it. Empty means no reference columns: the differ dropped
		// fld<AccountCr>_RTRef / _RRRef from the movements, and the credit totals trigger, which
		// reads NEW.fld<AccountCr>_RTRef, could then not be created at all.
		//
		// SyncAccountDimensionSlots runs FIRST because turning correspondence on creates the credit
		// account inside it — typing before that would type something that does not exist yet.
		if (m_accountCr != nullptr)
			m_accountCr->GetTypeDesc().SetDefaultMetaType(typeDesc);

		// ⭐ AND THEIR TYPES, IN THE SAME BREATH. Half of that asymmetry survived the last fix: the
		// slots were CREATED here and TYPED only by the run phase, so a save made in between built
		// the schema from typeless slots — one discriminator column each and nothing else — while
		// the configuration saved a moment later carried the types. From then on the two disagree,
		// and the next apply drops reference columns that were never created.
		ApplyAccountDimensionSlotTypes();
	}

	// Enable/disable Account field based on binding
	if ((*m_propertyAttributeAccount)->GetClsidCount() > 0)
		(*m_propertyAttributeAccount)->ClearFlag(metaDisableFlag);
	else
		(*m_propertyAttributeAccount)->SetFlag(metaDisableFlag);

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}
