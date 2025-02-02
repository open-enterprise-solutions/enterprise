////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "backend/metaData.h"

#include "backend/appData.h"
#include "reference/reference.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/systemManager/systemManager.h"

#include "backend/fileSystem/fs.h"
//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CRecordDataObjectCatalog::CRecordDataObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& objGuid, eObjectMode objMode) :
	IRecordDataObjectFolderRef(metaObject, objGuid, objMode)
{
}

CRecordDataObjectCatalog::CRecordDataObjectCatalog(const CRecordDataObjectCatalog& source) :
	IRecordDataObjectFolderRef(source)
{
}

CSourceExplorer CRecordDataObjectCatalog::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	CMetaObjectCatalog* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDataCode(), false);
		srcHelper.AppendSource(metaRef->GetDataDescription());
		CMetaObjectAttributeDefault* defOwner = metaRef->GetCatalogOwner();
		if (defOwner != nullptr && defOwner->GetClsidCount() > 0) {
			srcHelper.AppendSource(metaRef->GetCatalogOwner());
		}
		srcHelper.AppendSource(metaRef->GetDataParent());
	}

	for (auto& obj : m_metaObject->GetObjectAttributes()) {
		eItemMode attrUse = obj->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item
				|| attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
					srcHelper.AppendSource(obj);
				}
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
					srcHelper.AppendSource(obj);
				}
			}
		}
	}

	for (auto& obj : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = obj->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item
				|| tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(obj);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(obj);
			}
		}
	}

	return srcHelper;
}

void CRecordDataObjectCatalog::ShowFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	IBackendValueForm* valueForm =
		GetFormValue(formName, ownerControl);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

IBackendValueForm* CRecordDataObjectCatalog::GetFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm) {
		return foundedForm;
	}

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
		defList = m_metaObject->GetDefaultFormByID(m_objMode == eObjectMode::OBJECT_ITEM ? CMetaObjectCatalog::eFormObject : CMetaObjectCatalog::eFormGroup);
	}

	IBackendValueForm* valueForm = nullptr;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
		valueForm->Modify(m_objModified);
	}
	else {
		valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			this, m_objGuid
		);
		valueForm->BuildForm(m_objMode == eObjectMode::OBJECT_ITEM ? CMetaObjectCatalog::eFormObject : CMetaObjectCatalog::eFormGroup);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

#include "backend/backend_mainFrame.h"

bool CRecordDataObjectCatalog::WriteObject()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		if (!CBackendException::IsEvalMode())
		{
			IBackendValueForm* const valueForm = GetForm();

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

				if (!SaveData()) {
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

				if (backend_mainFrame != nullptr) {
					backend_mainFrame->RefreshFrame();
				}

				if (valueForm) {
					valueForm->UpdateForm();
					valueForm->Modify(false);
				}
			}

			m_objModified = false;
		}
	}

	return true;
}

bool CRecordDataObjectCatalog::DeleteObject()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		if (!CBackendException::IsEvalMode())
		{
			IBackendValueForm* const valueForm = GetForm();

			{
				db_query->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("BeforeDelete", cancel);
					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to delete object in db!")); return false;
					}
				}

				if (!DeleteData()) {
					db_query->RollBack(); CSystemFunction::Raise(_("failed to delete object in db!")); return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallAsProc("OnDelete", cancel);
					if (cancel.GetBoolean()) {
						db_query->RollBack(); CSystemFunction::Raise(_("failed to delete object in db!")); return false;
					}
				}

				db_query->Commit();

				if (valueForm) {
					valueForm->CloseForm();
				}

				if (backend_mainFrame != nullptr) {
					backend_mainFrame->RefreshFrame();
				}
			}
		}
	}

	return true;
}

enum Func {
	enIsNew = 0,
	enCopy,
	enFill,
	enWrite,
	enDelete,
	enModified,
	enGetForm,
	enGetMetadata
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CRecordDataObjectCatalog::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("isNew", "isNew()");
	m_methodHelper->AppendFunc("copy", "copy()");
	m_methodHelper->AppendFunc("fill", 1, "fill(object)");
	m_methodHelper->AppendFunc("write", "write()");
	m_methodHelper->AppendFunc("delete", "delete()");
	m_methodHelper->AppendFunc("modified", "modified()");
	m_methodHelper->AppendFunc("getFormObject", 3, "getFormObject(string, owner, guid)");
	m_methodHelper->AppendFunc("getMetadata", "getMetadata()");

	m_methodHelper->AppendProp(wxT("thisObject"), true, false, eThisObject, eSystem);

	//fill custom attributes 
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			true,
			!m_metaObject->IsDataReference(obj->GetMetaID()),
			obj->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables 
	for (auto& obj : m_metaObject->GetObjectTables()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			true,
			false,
			obj->GetMetaID(),
			eTable
		);
	}
	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_aExportFuncList) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_aExportVarList) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}
}

bool CRecordDataObjectCatalog::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum),
			varPropVal
		);
	}

	return false;
}

bool CRecordDataObjectCatalog::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (m_metaObject->IsDataReference(lPropData)) {
			pvarPropVal = GetReference();
			return true;
		}
		return GetValueByMetaID(lPropData, pvarPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisObject:
			pvarPropVal = GetValue();
			return true;
		}
	}
	return false;
}

bool CRecordDataObjectCatalog::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsNew:
		pvarRetValue = m_newObject;
		return true;
	case enCopy:
		pvarRetValue = CopyObject();
		return true;
	case enFill:
		FillObject(*paParams[0]);
		return true;
	case enWrite:
		WriteObject();
		return true;
	case enDelete:
		DeleteObject();
		return true;
	case enModified:
		pvarRetValue = m_objModified;
		return true;
	case enGetForm:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxEmptyString,
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr
		);
		return true;
	case enGetMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}

	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}