#include "accumulationRegister.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/systemManager/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

bool CRecordSetObjectAccumulationRegister::WriteRecordSet(bool replace, bool clearTable)
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		if (!CBackendException::IsEvalMode())
		{
			{
				db_query->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("BeforeWrite", cancel);

					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				if (!SaveData(replace, clearTable)) {
					db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
					return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				db_query->Commit();
			}

			m_objModified = false;
		}
	}

	return true;
}

bool CRecordSetObjectAccumulationRegister::DeleteRecordSet()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		if (!CBackendException::IsEvalMode())
		{
			{
				db_query->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("BeforeWrite", cancel);

					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				if (!DeleteData()) {
					db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
					return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				db_query->Commit();
			}

			m_objModified = false;
		}
	}

	return true;
}

enum func {
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

enum prop {
	eThisObject,
	eFilter
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CRecordSetObjectAccumulationRegister::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("add", "add()");
	m_methodHelper->AppendFunc("count", "count()");
	m_methodHelper->AppendFunc("clear", "clear()");
	m_methodHelper->AppendFunc("write", 1, "write(replace)");
	m_methodHelper->AppendFunc("load", 1, "load(table)");
	m_methodHelper->AppendFunc("unload", "unload()");
	m_methodHelper->AppendFunc("modified", "modified()");
	m_methodHelper->AppendFunc("read", "read()");
	m_methodHelper->AppendFunc("selected", "selected()");
	m_methodHelper->AppendFunc("getMetadata", "getMetadata()");

	m_methodHelper->AppendProp(wxT("thisObject"), true, false, eThisObject, wxNOT_FOUND);
	m_methodHelper->AppendProp(wxT("filter"), true, false, eFilter, wxNOT_FOUND);
}

bool CRecordSetObjectAccumulationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CRecordSetObjectAccumulationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eThisObject:
		pvarPropVal = this;
		return true;
	case eFilter:
		pvarPropVal = m_recordSetKeyValue;
		return true;
	}

	return false;
}

bool CRecordSetObjectAccumulationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = new CRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true; 
	case func::eCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case func::eClear:
		IValueTable::Clear();
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<IValueTable>());
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