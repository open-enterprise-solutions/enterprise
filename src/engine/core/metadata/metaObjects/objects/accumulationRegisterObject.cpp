#include "accumulationRegister.h"

#include "appData.h"
#include "frontend/visualView/controls/form.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/compiler/systemObjects.h"

#include "utils/stringUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/frontend/docView/docManager.h"

bool CRecordSetAccumulationRegister::WriteRecordSet(bool replace, bool clearTable)
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			{
				databaseLayer->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallFunction("BeforeWrite", cancel);

					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				if (!SaveData(replace, clearTable)) {
					databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
					return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				databaseLayer->Commit();
			}

			m_objModified = false;
		}
	}

	return true;
}

bool CRecordSetAccumulationRegister::DeleteRecordSet()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			{
				databaseLayer->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallFunction("BeforeWrite", cancel);

					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				if (!DeleteData()) {
					databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
					return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				databaseLayer->Commit();
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

void CRecordSetAccumulationRegister::PrepareNames() const
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

	m_methodHelper->AppendProp(wxT("thisObject"), true, false, eThisObject, s_def_alias);
	m_methodHelper->AppendProp(wxT("filter"), true, false, eFilter, s_def_alias);
}

bool CRecordSetAccumulationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CRecordSetAccumulationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
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

#include "frontend/visualView/controls/form.h"

bool CRecordSetAccumulationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = new CRecordSetRegisterReturnLine(this, GetItem(AppendRow()));
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