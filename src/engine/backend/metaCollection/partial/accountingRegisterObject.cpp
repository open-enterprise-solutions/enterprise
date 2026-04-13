////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register object (record set)
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"

//TODO: Implement WriteRecordSet, DeleteRecordSet, SaveVirtualTable, DeleteVirtualTable
// Following the same pattern as accumulationRegisterObject.cpp

bool ibValueRecordSetObjectAccountingRegister::WriteRecordSet(bool replace, bool clearTable) { return ibValueRecordSetObject::WriteRecordSet(replace, clearTable); }
bool ibValueRecordSetObjectAccountingRegister::DeleteRecordSet() { return ibValueRecordSetObject::DeleteRecordSet(); }
bool ibValueRecordSetObjectAccountingRegister::SaveVirtualTable() { return true; }
bool ibValueRecordSetObjectAccountingRegister::DeleteVirtualTable() { return true; }

enum Func { enWrite = 0, enRead, enModified, enClear, enGetMetadata };

void ibValueRecordSetObjectAccountingRegister::PrepareNames() const
{
	ibValueRecordSetObject::PrepareNames();
}

bool ibValueRecordSetObjectAccountingRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return ibValueRecordSetObject::SetPropVal(lPropNum, varPropVal);
}

bool ibValueRecordSetObjectAccountingRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectAccountingRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return ibValueRecordSetObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
