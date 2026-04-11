#include "informationRegister.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"

bool ibValueRecordSetObjectInformationRegister::WriteRecordSet(bool replace, bool clearTable)
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			if (!m_metaObject->AccessRight_Write()) {
				ibBackendAccessException::Error();
				return false;
			}

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				db_query_active_transaction.BeginTransaction();

				{
					ibValue cancel = false;
					m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);

					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				if (!SaveData(replace, clearTable)) {
					db_query_active_transaction.RollBackTransaction();
					ibBackendCoreException::Error(_("Failed to write object in db!"));
					return false;
				}

				{
					ibValue cancel = false;
					m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				db_query_active_transaction.CommitTransaction();
			}

			m_objModified = false;
		}
	}

	return true;
}

bool ibValueRecordSetObjectInformationRegister::DeleteRecordSet()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			if (!m_metaObject->AccessRight_Delete()) {
				ibBackendAccessException::Error();
				return false;
			}

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				db_query_active_transaction.BeginTransaction();

				{
					ibValue cancel = false;
					m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);

					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				if (!DeleteData()) {
					db_query_active_transaction.RollBackTransaction();
					ibBackendCoreException::Error(_("Failed to write object in db!"));
					return false;
				}

				{
					ibValue cancel = false;
					m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}
				}

				db_query_active_transaction.CommitTransaction();
			}

			m_objModified = false;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ibSourceExplorer ibValueRecordManagerObjectInformationRegister::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	ibValueMetaObjectInformationRegister* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		if (metaRef->GetPeriodicity() != ibPeriodicity::eNonPeriodic) {
			srcHelper.AppendSource(metaRef->GetRegisterPeriod());
		}
	}

	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
		srcHelper.AppendSource(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		srcHelper.AppendSource(object);
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		srcHelper.AppendSource(object);
	}

	return srcHelper;
}

#pragma region _form_builder_h_
void ibValueRecordManagerObjectInformationRegister::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	ibBackendValueForm* const valueForm =
		GetFormValue(strFormName, ownerControl);

	if (valueForm != nullptr) {
		valueForm->Modify(m_recordSet->IsModified());
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordManagerObjectInformationRegister::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr) {

		ibBackendValueForm* createdForm = m_metaObject->CreateAndBuildForm(
			strFormName,
			ibValueMetaObjectInformationRegister::eFormRecord,
			ownerControl,
			this,
			m_objGuid
		);

		if (createdForm != nullptr)
			createdForm->CloseOnOwnerClose(false);

		return createdForm;
	}

	return foundedForm;
}
#pragma endregion

bool ibValueRecordManagerObjectInformationRegister::WriteRegister(bool replace)
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			ibTransactionGuard db_query_active_transaction = db_query;
			{
				db_query_active_transaction.BeginTransaction();

				bool newObject = ibValueRecordManagerObjectInformationRegister::IsNewObject();

				if (!SaveData()) {
					db_query_active_transaction.RollBackTransaction();
					ibBackendCoreException::Error(_("failed to save object in db!"));
					return false;
				}

				ibBackendValueForm::UpdateFormUniqueKey(m_objGuid);

				db_query_active_transaction.CommitTransaction();

				ibBackendValueForm* const valueForm = GetForm();

				if (newObject && valueForm != nullptr) valueForm->NotifyCreate(GetValue());
				else if (valueForm != nullptr) valueForm->NotifyChange(GetValue());
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

bool ibValueRecordManagerObjectInformationRegister::DeleteRegister()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			ibTransactionGuard db_query_active_transaction = db_query;
			{
				ibBackendValueForm* const valueForm = GetForm();
				{
					db_query_active_transaction.BeginTransaction();

					if (!DeleteData()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to delete object in db!"));
						return false;
					}

					db_query_active_transaction.CommitTransaction();

					if (valueForm != nullptr) valueForm->NotifyDelete(GetValue());
				}
				m_recordSet->Modify(false);
			}
		}
	}

	return true;
}

enum recordManager
{
	enCopyRecordManager,
	enWriteRecordManager,
	enDeleteRecordManager,
	enModifiedRecordManager,
	enReadRecordManager,
	enSelectedRecordManager,
	enGetFormRecord,
	enGetTemplate,
	enGetMetadataRecordManager
};

enum recordSet
{
	enAdd = 0,
	enCount,
	enClear,
	enLoad,
	enUnload,
	enWriteRecordSet,
	enModifiedRecordSet,
	enReadRecordSet,
	enSelectedRecordSet,
	enGetMetadataRecordSet,
};

enum prop
{
	eThisObject,
	eFilter
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectInformationRegister::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("Add"), wxT("Add()"));
	m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"));
	m_methodHelper->AppendFunc(wxT("Clear"), wxT("Clear()"));
	m_methodHelper->AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	m_methodHelper->AppendFunc(wxT("Load"), 1, wxT("Load(value : any table)"));
	m_methodHelper->AppendFunc(wxT("Unload"), wxT("Unload()"));
	m_methodHelper->AppendFunc(wxT("Modified"), wxT("Modified()"));
	m_methodHelper->AppendFunc(wxT("Read"), wxT("Read()"));
	m_methodHelper->AppendFunc(wxT("Selected"), wxT("Selected()"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	m_methodHelper->AppendProp(wxT("ThisObject"), true, false, prop::eThisObject, wxNOT_FOUND);
	m_methodHelper->AppendProp(wxT("Filter"), true, false, prop::eFilter, wxNOT_FOUND);
}

void ibValueRecordManagerObjectInformationRegister::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("Copy"), wxT("Copy()"));
	m_methodHelper->AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	m_methodHelper->AppendFunc(wxT("Delete"), wxT("Delete()"));
	m_methodHelper->AppendFunc(wxT("Modified"), wxT("Modified()"));
	m_methodHelper->AppendFunc(wxT("Read"), wxT("Read()"));
	m_methodHelper->AppendFunc(wxT("Selected"), wxT("Selected()"));
	m_methodHelper->AppendFunc(wxT("GetFormRecord"), 3, wxT("GetFormRecord(name : string, owner : any, id : guid)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("getMetadata()"));

	//set object name 
	wxString objectName;

	//fill custom object 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		m_methodHelper->AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

bool ibValueRecordManagerObjectInformationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)       //setting attribute
{
	return SetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), varPropVal
	);
}

bool ibValueRecordManagerObjectInformationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), pvarPropVal
	);
}

//////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectInformationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectInformationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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

bool ibValueRecordSetObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		ibValueModelTableBase::Clear();
		return true;
	case recordSet::enLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModelTableBase>());
		return true;
	case recordSet::enUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case recordSet::enWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordSet::enModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case recordSet::enReadRecordSet:
		Read();
		return true;
	case recordSet::enSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case recordSet::enGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

bool ibValueRecordManagerObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case recordManager::enCopyRecordManager:
		pvarRetValue = CopyRegister();
		return true;
	case recordManager::enWriteRecordManager:
		pvarRetValue = WriteRegister(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordManager::enDeleteRecordManager:
		pvarRetValue = DeleteRegister();
		return true;
	case recordManager::enModifiedRecordManager:
		pvarRetValue = m_recordSet->IsModified();
		return true;
	case recordManager::enReadRecordManager:
		m_recordSet->Read();
		return true;
	case recordManager::enSelectedRecordManager:
		pvarRetValue = m_recordSet->Selected();
		return true;
	case recordManager::enGetFormRecord:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case recordManager::enGetMetadataRecordManager:
		pvarRetValue = m_metaObject;
		return true;
	}

	return false;
}