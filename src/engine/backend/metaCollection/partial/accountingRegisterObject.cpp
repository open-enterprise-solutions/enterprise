////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register object (record set)
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

// WriteRecordSet / DeleteRecordSet inherited from ibValueRecordSetObject
// (Phase B template-method) — the scaffold is in commonObject.cpp; the
// Begin/Commit + LockByKeys helpers it calls live in commonObjectRecordSetQuery.cpp.


enum func
{
	eAdd = 0,
	eCount,
	eClear,
	eLoad,
	eUnload,
	eWriteRecordSet,
	eModifiedRecordSet,
	eReadRecordSet,
	eSelectedRecordSet,
	eGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectAccountingRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value: table)"));
	helper.AppendFunc(wxT("Unload"), wxT("Unload()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	// ThisObject + Filter are bound in InitializeObject (context / export) —
	// no manual prop AppendProp on the record-set helper.
}

bool ibValueRecordSetObjectAccountingRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectAccountingRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return false;
}

bool ibValueRecordSetObjectAccountingRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case func::eCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case func::eClear:
		ibValueModelStorage::Clear();
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case func::eUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case func::eWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case func::eModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case func::eReadRecordSet:
		Read();
		return true;
	case func::eSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case func::eGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}
