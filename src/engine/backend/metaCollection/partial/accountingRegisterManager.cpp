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

	helper.AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	helper.AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	helper.AppendFunc(wxT("Balance"), arity(ibAcctShape::Balance), correspondence
		? wxT("Balance(period, accountDr, accountCr, accountDimensions, condition)")
		: wxT("Balance(period, account, accountDimensions, condition)"));
	helper.AppendFunc(wxT("Turnovers"), arity(ibAcctShape::Turnovers), correspondence
		? wxT("Turnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensions, condition, periodicity)")
		: wxT("Turnovers(beginOfPeriod, endOfPeriod, account, accountDimensions, condition, periodicity)"));
	helper.AppendFunc(wxT("DrCrTurnovers"), arity(ibAcctShape::DrCrTurnovers),
		wxT("DrCrTurnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensionsDr, accountDimensionsCr, condition)"));
	helper.AppendFunc(wxT("BalanceAndTurnovers"), arity(ibAcctShape::BalanceAndTurnovers), correspondence
		? wxT("BalanceAndTurnovers(beginOfPeriod, endOfPeriod, accountDr, accountCr, accountDimensions, condition, periodicity)")
		: wxT("BalanceAndTurnovers(beginOfPeriod, endOfPeriod, account, accountDimensions, condition, periodicity)"));
	helper.AppendFunc(wxT("RecordsWithAccountDimensions"), arity(ibAcctShape::Records), correspondence
		? wxT("RecordsWithAccountDimensions(beginOfPeriod, endOfPeriod, accountDimensionsDr, accountDimensionsCr, condition)")
		: wxT("RecordsWithAccountDimensions(beginOfPeriod, endOfPeriod, accountDimensions, condition)"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectAccountingRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
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
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
