#include "chartOfAccounts.h"
#include "accountingRegister.h"   // the registers that build their columns from this chart's count
#include "backend/metaData.h"
#include "backend/objCtor.h"

// EVERY ACCOUNTING REGISTER IN THE CONFIGURATION, asked to rebuild its slot set.
//
// The count lives here, on the chart, but the COLUMNS live on the registers — so a change made in
// one place has to reach the other, and the registers are the ones that know whether they are bound
// to this chart at all (SyncAccountDimensionSlots asks its own binding and ignores a chart that is
// not its own). Hence a sweep rather than a subscription list: nothing has to be registered, and a
// register added later is found by the same walk.
static void ibResyncAccountingRegisters(ibValueMetaObject* node)
{
	if (node == nullptr)
		return;

	for (unsigned int i = 0; i < node->GetChildCount(); i++) {
		ibValueMetaObject* child = node->GetChild(i);
		if (child == nullptr)
			continue;

		ibValueMetaObjectAccountingRegister* reg = nullptr;
		if (child->ConvertToValue(reg) && reg != nullptr)
			reg->SyncAccountDimensionSlots();

		ibResyncAccountingRegisters(child);
	}
}

void ibValueMetaObjectChartOfAccounts::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectChartOfAccounts::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectChartOfAccounts::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);

	// The number of analytical slots, and which values they may hold, are both declared HERE and
	// both build columns THERE. Waiting for the next configuration run would leave a register
	// describing the previous shape while the designer already shows the new one.
	if (property == m_propertyMaxAccountDimensionCount ||
		property == m_propertyChartOfCharacteristicTypes) {
		// The ceiling also decides how many UNFOLDED columns the list has — the same number, the same
		// moment. Sync first, then type: a column created here would otherwise stand untyped until the
		// next run, which is the disagreement between metadata and schema this whole area guards against.
		if (property == m_propertyMaxAccountDimensionCount)
			SyncAccountDimensionKindColumns();
		// The kinds column takes its type from the binding the user has just changed — apply it here for
		// the same reason the registers resync here: the designer already shows the new shape.
		if (property == m_propertyChartOfCharacteristicTypes)
			ApplyAccountDimensionKindType();
		if (m_metaData != nullptr)
			ibResyncAccountingRegisters(m_metaData->GetCommonMetaObject());
	}
}
