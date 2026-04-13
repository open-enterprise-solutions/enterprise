////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register manager
////////////////////////////////////////////////////////////////////////////

#include "accountingRegisterManager.h"
#include "backend/metaData.h"
#include "commonObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueManagerDataObjectAccountingRegister, ibValue);

ibValueMetaObjectCommonModule* ibValueManagerDataObjectAccountingRegister::GetModuleManager() const
{
	return m_metaObject->GetModuleManager();
}

enum Func {
	eCreateRecordSet,
	eCreateRecordKey,
	eBalance,
	eTurnover,
	eDrCrTurnover,
	eBalanceAndTurnover,
	eSelect,
	eGetForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectAccountingRegister::PrepareNames() const
{
	ibValueManagerDataObject::PrepareNames();

	m_methodHelper->AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	m_methodHelper->AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	m_methodHelper->AppendFunc(wxT("Balance"), 3, wxT("Balance(period, account, filter...)"));
	m_methodHelper->AppendFunc(wxT("Turnovers"), 4, wxT("Turnovers(beginOfPeriod, endOfPeriod, account, filter...)"));
	m_methodHelper->AppendFunc(wxT("DrCrTurnovers"), 4, wxT("DrCrTurnovers(beginOfPeriod, endOfPeriod, account, filter...)"));
	m_methodHelper->AppendFunc(wxT("BalanceAndTurnovers"), 4, wxT("BalanceAndTurnovers(beginOfPeriod, endOfPeriod, account, filter...)"));
	m_methodHelper->AppendFunc(wxT("Select"), wxT("Select()"));
	m_methodHelper->AppendFunc(wxT("GetForm"), 3, wxT("GetForm(string, owner, guid)"));
	m_methodHelper->AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(string)"));
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
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueRecordKeyObject>(m_metaObject);
		return true;
	case eBalance:
		pvarRetValue = lSizeArray > 2 ?
			Balance(*paParams[0], *paParams[1], *paParams[2]) :
			lSizeArray > 1 ? Balance(*paParams[0], *paParams[1]) : Balance(*paParams[0]);
		return true;
	case eTurnover:
		pvarRetValue = lSizeArray > 3 ?
			Turnovers(*paParams[0], *paParams[1], *paParams[2], *paParams[3]) :
			lSizeArray > 2 ? Turnovers(*paParams[0], *paParams[1], *paParams[2]) :
			Turnovers(*paParams[0], *paParams[1]);
		return true;
	case eDrCrTurnover:
		pvarRetValue = lSizeArray > 3 ?
			DrCrTurnovers(*paParams[0], *paParams[1], *paParams[2], *paParams[3]) :
			lSizeArray > 2 ? DrCrTurnovers(*paParams[0], *paParams[1], *paParams[2]) :
			DrCrTurnovers(*paParams[0], *paParams[1]);
		return true;
	case eBalanceAndTurnover:
		pvarRetValue = lSizeArray > 3 ?
			BalanceAndTurnovers(*paParams[0], *paParams[1], *paParams[2], *paParams[3]) :
			lSizeArray > 2 ? BalanceAndTurnovers(*paParams[0], *paParams[1], *paParams[2]) :
			BalanceAndTurnovers(*paParams[0], *paParams[1]);
		return true;
	case eSelect:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSelectorRegisterDataObject>(m_metaObject);
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
