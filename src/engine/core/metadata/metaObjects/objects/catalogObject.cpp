////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "core/metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/compiler/systemObjects.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CObjectCatalog::CObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& objGuid, eObjectMode objMode) :
	IRecordDataObjectFolderRef(metaObject, objGuid, objMode)
{
}

CObjectCatalog::CObjectCatalog(const CObjectCatalog& source) :
	IRecordDataObjectFolderRef(source)
{
}

CSourceExplorer CObjectCatalog::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		false
	);

	CMetaObjectCatalog* metaRef = NULL;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDataCode(), false);
		srcHelper.AppendSource(metaRef->GetDataDescription());
		CMetaDefaultAttributeObject* defOwner = metaRef->GetCatalogOwner();
		if (defOwner != NULL && defOwner->GetClsidCount() > 0) {
			srcHelper.AppendSource(metaRef->GetCatalogOwner());
		}
		srcHelper.AppendSource(metaRef->GetDataParent());
	}

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		eItemMode attrUse = attribute->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item
				|| attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute);
				}
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute);
				}
			}
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = table->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item
				|| tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
	}

	return srcHelper;
}

void CObjectCatalog::ShowFormValue(const wxString& formName, IControlFrame* ownerControl)
{
	CValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm =
		GetFormValue(formName, ownerControl);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

CValueForm* CObjectCatalog::GetFormValue(const wxString& formName, IControlFrame* ownerControl)
{
	CValueForm* const foundedForm = GetForm();

	if (foundedForm) {
		return foundedForm;
	}

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
		defList = m_metaObject->GetDefaultFormByID(m_objMode == eObjectMode::OBJECT_ITEM ? CMetaObjectCatalog::eFormObject : CMetaObjectCatalog::eFormGroup);
	}

	CValueForm* valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
		valueForm->Modify(m_objModified);
	}
	else {
		valueForm = new CValueForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(m_objMode == eObjectMode::OBJECT_ITEM ? CMetaObjectCatalog::eFormObject : CMetaObjectCatalog::eFormGroup);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "core/compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

#include "core/frontend/docView/docManager.h"

bool CObjectCatalog::WriteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* const valueForm = GetForm();

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

				if (!SaveData()) {
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

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
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

bool CObjectCatalog::DeleteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* const valueForm = GetForm();

			{
				databaseLayer->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallFunction("BeforeDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
					}
				}

				if (!DeleteData()) {
					databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
					}
				}

				databaseLayer->Commit();

				if (valueForm) {
					valueForm->CloseForm();
				}

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
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

void CObjectCatalog::PrepareNames() const
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
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attribute->GetName(),
			true,
			!m_metaObject->IsDataReference(attribute->GetMetaID()),
			attribute->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables 
	for (auto table : m_metaObject->GetObjectTables()) {
		if (table->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			table->GetName(),
			true,
			false,
			table->GetMetaID(),
			eTable
		);
	}
	if (m_procUnit != NULL) {
		byteCode_t* byteCode = m_procUnit->GetByteCode();
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

bool CObjectCatalog::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
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

bool CObjectCatalog::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
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

bool CObjectCatalog::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL
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