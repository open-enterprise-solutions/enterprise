#include "informationRegister.h"

#include "appData.h"
#include "frontend/visualView/controls/form.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/compiler/systemObjects.h"

#include "utils/stringUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/frontend/docView/docManager.h"

bool CRecordSetInformationRegister::WriteRecordSet(bool replace, bool clearTable)
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

bool CRecordSetInformationRegister::DeleteRecordSet()
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

////////////////////////////////////////////////////////////////////////////////////////////////////

CSourceExplorer CRecordManagerInformationRegister::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		false
	);

	CMetaObjectInformationRegister* metaRef = NULL;

	if (m_metaObject->ConvertToValue(metaRef)) {
		if (metaRef->GetPeriodicity() != ePeriodicity::eNonPeriodic) {
			srcHelper.AppendSource(metaRef->GetRegisterPeriod());
		}
	}

	for (auto attribute : m_metaObject->GetObjectDimensions()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto attribute : m_metaObject->GetObjectResources()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	return srcHelper;
}

void CRecordManagerInformationRegister::ShowFormValue(const wxString& formName, IControlFrame* owner)
{
	CValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm = GetFormValue(formName, owner);

	valueForm->Modify(m_recordSet->IsModified());
	valueForm->ShowForm();
}

CValueForm* CRecordManagerInformationRegister::GetFormValue(const wxString& formName, IControlFrame* ownerControl)
{
	IMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : m_metaObject->GetObjectForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectInformationRegister::eFormRecord);
	}

	CValueForm* valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
		valueForm->Modify(m_recordSet->IsModified());
	}
	else {
		valueForm = new CValueForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormRecord);
		valueForm->Modify(m_recordSet->IsModified());
	}

	return valueForm;
}

#include "appData.h"

#include "core/frontend/docView/docManager.h"
#include "core/compiler/systemObjects.h"

bool CRecordManagerInformationRegister::WriteRegister(bool replace)
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			{
				databaseLayer->BeginTransaction();

				if (!SaveData()) {
					databaseLayer->RollBack();
					CSystemObjects::Raise(_("failed to save object in db!"));
					return false;
				}

				CValueForm::UpdateFormKey(m_objGuid);

				databaseLayer->Commit();

				CValueForm* const valueForm = GetForm();

				if (valueForm != NULL) {
					valueForm->UpdateForm();
					valueForm->Modify(false);
				}

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
				}
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

bool CRecordManagerInformationRegister::DeleteRegister()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm * const valueForm = GetForm();

			{
				databaseLayer->BeginTransaction();

				if (!DeleteData()) {
					databaseLayer->RollBack();
					CSystemObjects::Raise(_("failed to delete object in db!"));
					return false;
				}

				databaseLayer->Commit();

				if (valueForm) {
					valueForm->CloseForm();
				}

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
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

void CRecordSetInformationRegister::PrepareNames() const
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

	m_methodHelper->AppendProp(wxT("thisObject"), true, false, eThisObject, s_def_alias);
	m_methodHelper->AppendProp(wxT("filter"), true, false, eFilter, s_def_alias);
}

void CRecordManagerInformationRegister::PrepareNames() const
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

	//fill custom attributes 
	for (auto attributes : m_metaObject->GetGenericAttributes()) {
		if (attributes->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attributes->GetName(),
			attributes->GetMetaID()
		);
	}
}

bool CRecordManagerInformationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)       //установка атрибута
{
	return SetValueByMetaID(
		m_methodHelper->GetPropAlias(lPropNum), varPropVal
	);
}

bool CRecordManagerInformationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return GetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), pvarPropVal
	);
}

//////////////////////////////////////////////////////////////////////////

bool CRecordSetInformationRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CRecordSetInformationRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
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

bool CRecordSetInformationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new CRecordSetRegisterReturnLine(this, GetItem(AppendRow()));
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

bool CRecordManagerInformationRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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