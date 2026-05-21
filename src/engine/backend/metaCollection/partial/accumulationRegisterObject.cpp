#include "accumulationRegister.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectAccumulationRegister::WriteRecordSet(bool replace, bool clearTable)
{
	if (!appData->DesignerMode() || ibApplicationData::DesignerDataWriteEnabled())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			if (!m_metaObject->AccessRight_Write()) {
				ibBackendAccessException::Error();
				return false;
			}

			{
				scope.SafeBeginTransaction();

				{
					ibValue cancel = false;
					ExecAsProc(wxT("BeforeWrite"), cancel);

					if (cancel.GetBoolean()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				if (!SaveData(replace, clearTable)) {
					scope.SafeRollBackTransaction();
					ibBackendCoreException::Error(_("Failed to write object in db!"));
					return false;
				}

				{
					ibValue cancel = false;
					ExecAsProc(wxT("OnWrite"), cancel);
					if (cancel.GetBoolean()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				scope.SafeCommitTransaction();
			}

			m_objModified = false;
		}
	}

	return true;
}

bool ibValueRecordSetObjectAccumulationRegister::DeleteRecordSet()
{
	if (!appData->DesignerMode())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			if (!m_metaObject->AccessRight_Delete()) {
				ibBackendAccessException::Error();
				return false;
			}

			{
				scope.SafeBeginTransaction();

				{
					ibValue cancel = false;
					ExecAsProc(wxT("BeforeWrite"), cancel);

					if (cancel.GetBoolean()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				if (!DeleteData()) {
					scope.SafeRollBackTransaction();
					ibBackendCoreException::Error(_("Failed to write object in db!"));
					return false;
				}

				{
					ibValue cancel = false;
					ExecAsProc(wxT("OnWrite"), cancel);
					if (cancel.GetBoolean()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				scope.SafeCommitTransaction();
			}

			m_objModified = false;
		}
	}

	return true;
}

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

enum prop
{
	eThisObject,
	eFilter
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectAccumulationRegister::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("Add"), wxT("Add()"));
	m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"));
	m_methodHelper->AppendFunc(wxT("Clear"), wxT("Clear()"));
	m_methodHelper->AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	m_methodHelper->AppendFunc(wxT("Load"), 1, wxT("Load(value: table)"));
	m_methodHelper->AppendFunc(wxT("Unload"), wxT("Unload()"));
	m_methodHelper->AppendFunc(wxT("Modified"), wxT("Modified()"));
	m_methodHelper->AppendFunc(wxT("Read"), wxT("Read()"));
	m_methodHelper->AppendFunc(wxT("Selected"), wxT("Selected()"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	m_methodHelper->AppendProp(wxT("ThisObject"), true, false, true, prop::eThisObject, wxNOT_FOUND);
	m_methodHelper->AppendProp(wxT("Filter"), true, false, prop::eFilter, wxNOT_FOUND);
}

bool ibValueRecordSetObjectAccumulationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectAccumulationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case prop::eThisObject:
		pvarPropVal = this;
		return true;
	case prop::eFilter:
		pvarPropVal = m_recordSetKeyValue;
		return true;
	}

	return false;
}

bool ibValueRecordSetObjectAccumulationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, GetItem(AppendRow()));
		return true;
	case func::eCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case func::eClear:
		ibValueModelRamTableBase::Clear();
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModelTableBase>());
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
