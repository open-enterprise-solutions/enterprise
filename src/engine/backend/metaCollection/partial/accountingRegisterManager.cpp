////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register manager
////////////////////////////////////////////////////////////////////////////

#include "accountingRegisterManager.h"
#include "backend/metaData.h"
#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectAccountingRegister::GetManagerModule() const
{
	return m_metaObject->GetManagerModule();
}

enum Func {
	eCreateRecordSet,
	eCreateRecordKey,
	eBalance,
	eTurnover,
	eDrCrTurnover,
	eBalanceAndTurnover,
	eRecordsWithAccountDimensions,
	eSelect,
	eGetForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectAccountingRegister::FillManagerMethods(ibMemberTable& helper) const
{
	// ⭐⭐ THE ARITY IS ASKED OF THE LAYOUT, NEVER WRITTEN BESIDE IT.
	//
	// Hand-written, it drifted the first day: `ibAcctArgs::For` reserves a slot for the credit-side
	// breakdown in correspondence mode, the numbers here did not, and a script's CONDITION landed in
	// the kinds slot — read as a breakdown list and silently dropped. Exactly the neighbour's
	// off-by-one this register's computed layout was built to make impossible, reintroduced one file
	// away by a literal.
	//
	// So the count comes from the same function that reads the call. A parameter added to the layout
	// changes both ends at once, and there is no second list to forget.
	const bool correspondence = m_metaObject != nullptr && m_metaObject->IsCorrespondence();
	const auto arity = [correspondence](ibAcctShape shape) {
		return static_cast<long>(ibAcctArgs::For(shape, correspondence).m_count);
	};

	helper.AppendFunc(ibT("CreateRecordSet"), ibT("CreateRecordSet()"));
	helper.AppendFunc(ibT("CreateRecordKey"), ibT("CreateRecordKey()"));
	helper.AppendFunc(ibT("Balance"), arity(ibAcctShape::Balance), correspondence
		? ibT("Balance(period, accountDr, accountCr, accountDimensions, condition)")
		: ibT("Balance(period, account, accountDimensions, condition)"));
	helper.AppendFunc(ibT("Turnovers"), arity(ibAcctShape::Turnovers), correspondence
		? ibT("Turnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensions, condition, periodicity)")
		: ibT("Turnovers(beginOfPeriod, endOfPeriod, account, accountDimensions, condition, periodicity)"));
	helper.AppendFunc(ibT("DrCrTurnovers"), arity(ibAcctShape::DrCrTurnovers),
		ibT("DrCrTurnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensionsDr, accountDimensionsCr, condition)"));
	helper.AppendFunc(ibT("BalanceAndTurnovers"), arity(ibAcctShape::BalanceAndTurnovers), correspondence
		? ibT("BalanceAndTurnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensions, condition, periodicity)")
		: ibT("BalanceAndTurnovers(beginOfPeriod, endOfPeriod, account, accountDimensions, condition, periodicity)"));
	helper.AppendFunc(ibT("RecordsWithAccountDimensions"), arity(ibAcctShape::Records), correspondence
		? ibT("RecordsWithAccountDimensions(beginOfPeriod, endOfPeriod, accountDimensionsDr, accountDimensionsCr, condition)")
		: ibT("RecordsWithAccountDimensions(beginOfPeriod, endOfPeriod, accountDimensions, condition)"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectAccountingRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	// ⭐ THE ARGUMENTS TRAVEL AS DATA, not as a ladder of arities. Every one of these used to be a
	// three-way ternary counting how many parameters arrived and calling a different overload for each
	// — the same list written four times, and the shape it assumed (period, account, filter) is not
	// even this register's shape any more. The reading reads the array by its own layout, which is the
	// only thing that knows what position 3 means for THIS register.
	case eBalance:
		pvarRetValue = Balance(paParams, lSizeArray);
		return true;
	case eTurnover:
		pvarRetValue = Turnovers(paParams, lSizeArray);
		return true;
	case eDrCrTurnover:
		pvarRetValue = DrCrTurnovers(paParams, lSizeArray);
		return true;
	case eBalanceAndTurnover:
		pvarRetValue = BalanceAndTurnovers(paParams, lSizeArray);
		return true;
	case eRecordsWithAccountDimensions:
		pvarRetValue = RecordsWithAccountDimensions(paParams, lSizeArray);
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRegisterDataObject(m_metaObject);
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
