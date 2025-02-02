#include "informationRegister.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/systemManager/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"

bool CRecordSetObjectInformationRegister::WriteRecordSet(bool replace, bool clearTable)
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

bool CRecordSetObjectInformationRegister::DeleteRecordSet()
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

////////////////////////////////////////////////////////////////////////////////////////////////////

CSourceExplorer CRecordManagerObjectInformationRegister::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	CMetaObjectInformationRegister* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		if (metaRef->GetPeriodicity() != ePeriodicity::eNonPeriodic) {
			srcHelper.AppendSource(metaRef->GetRegisterPeriod());
		}
	}

	for (auto& obj : m_metaObject->GetObjectDimensions()) {
		srcHelper.AppendSource(obj);
	}

	for (auto& obj : m_metaObject->GetObjectResources()) {
		srcHelper.AppendSource(obj);
	}

	for (auto& obj : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(obj);
	}

	return srcHelper;
}

void CRecordManagerObjectInformationRegister::ShowFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	IBackendValueForm* valueForm = GetFormValue(formName, ownerControl);

	valueForm->Modify(m_recordSet->IsModified());
	valueForm->ShowForm();
}

IBackendValueForm* CRecordManagerObjectInformationRegister::GetFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : m_metaObject->GetObjectForms()) {
			if (stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectInformationRegister::eFormRecord);
	}

	IBackendValueForm* valueForm = nullptr;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
		valueForm->Modify(m_recordSet->IsModified());
	}
	else {
		valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormRecord);
		valueForm->Modify(m_recordSet->IsModified());
	}

	return valueForm;
}

bool CRecordManagerObjectInformationRegister::WriteRegister(bool replace)
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

				if (!SaveData()) {
					db_query->RollBack();
					CSystemFunction::Raise(_("failed to save object in db!"));
					return false;
				}

				IBackendValueForm::UpdateFormUniqueKey(m_objGuid);

				db_query->Commit();

				IBackendValueForm* const valueForm = GetForm();

				if (valueForm != nullptr) {
					valueForm->UpdateForm();
					valueForm->Modify(false);
				}

				if (backend_mainFrame != nullptr) {
					backend_mainFrame->RefreshFrame();
				}
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

bool CRecordManagerObjectInformationRegister::DeleteRegister()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		if (!CBackendException::IsEvalMode())
		{
			IBackendValueForm * const valueForm = GetForm();
			{
				db_query->BeginTransaction();

				if (!DeleteData()) {
					db_query->RollBack();
					CSystemFunction::Raise(_("failed to delete object in db!"));
					return false;
				}

				db_query->Commit();

				if (valueForm) {
					valueForm->CloseForm();
				}

				if (backend_mainFrame != nullptr) {
					backend_mainFrame->RefreshFrame();
				}
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

enum recordManager {
	enCopyRecordManager,
	enWriteRecordManager,
	enDeleteRecordManager,
	enModifiedRecordManager,
	enReadRecordManager,
	enSelectedRecordManager,
	enGetFormRecord,
	enGetMetadataRecordManager
};

enum recordSet {
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

enum prop {
	eThisObject,
	eFilter
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CRecordSetObjectInformationRegister::PrepareNames() const
{
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

void CRecordManagerObjectInformationRegister::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("copy", "copy()");
	m_methodHelper->AppendFunc("write", 1, "write(replace)");
	m_methodHelper->AppendFunc("delete", "delete()");
	m_methodHelper->AppendFunc("modified", "modified()");
	m_methodHelper->AppendFunc("read", "read()");
	m_methodHelper->AppendFunc("selected", "selected()");
	m_methodHelper->AppendFunc("getFormRecord", 3, "getFormRecord(string, owner, guid)");
	m_methodHelper->AppendFunc("getMetadata", "getMetadata()");

	//fill custom obj 
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID()
		);
	}
}

bool CRecordManagerObjectInformationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)       //установка атрибута
{
	return SetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), varPropVal
	);
}

bool CRecordManagerObjectInformationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return GetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), pvarPropVal
	);
}

//////////////////////////////////////////////////////////////////////////

bool CRecordSetObjectInformationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CRecordSetObjectInformationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
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

bool CRecordSetObjectInformationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new CRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		IValueTable::Clear();
		return true;
	case recordSet::enLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<IValueTable>());
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

bool CRecordManagerObjectInformationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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
		pvarRetValue = GetFormValue();
		return true;
	case recordManager::enGetMetadataRecordManager:
		pvarRetValue = m_metaObject;
		return true;
	}

	return false;
}