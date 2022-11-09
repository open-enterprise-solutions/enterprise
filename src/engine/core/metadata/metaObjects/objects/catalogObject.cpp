////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

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
		m_metaObject, GetClassType(),
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
		CMetaGroupAttributeObject* metaAttr = NULL; eUseItem attrUse = eUseItem::eUseItem_Folder_Item;
		if (attribute->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetAttrUse();
		}
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eUseItem::eUseItem_Item
				|| attrUse == eUseItem::eUseItem_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute);
				}
			}
		}
		else {
			if (attrUse == eUseItem::eUseItem_Folder ||
				attrUse == eUseItem::eUseItem_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute);
				}
			}
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		CMetaGroupTableObject* metaTable = NULL; eUseItem tableUse = eUseItem::eUseItem_Folder_Item;
		if (table->ConvertToValue(metaTable)) {
			tableUse = metaTable->GetTableUse();
		}
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eUseItem::eUseItem_Item
				|| tableUse == eUseItem::eUseItem_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
		else {
			if (tableUse == eUseItem::eUseItem_Folder ||
				tableUse == eUseItem::eUseItem_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
	}

	return srcHelper;
}

void CObjectCatalog::ShowFormValue(const wxString& formName, IValueFrame* ownerControl)
{
	CValueForm* foundedForm = GetForm();

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

CValueForm* CObjectCatalog::GetFormValue(const wxString& formName, IValueFrame* ownerControl)
{
	CValueForm* foundedForm = GetForm();

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
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(m_objMode == eObjectMode::OBJECT_ITEM ? CMetaObjectCatalog::eFormObject : CMetaObjectCatalog::eFormGroup);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

bool CObjectCatalog::FillObject(CValue& vFillObject)
{
	return Filling(vFillObject);
}

CValue CObjectCatalog::CopyObject()
{
	if (appData->EnterpriseMode()) {
		return CopyObjectValue();
	}

	return CValue();
}

#include "common/docManager.h"

bool CObjectCatalog::WriteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* valueForm = GetForm();

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
			CValueForm* valueForm = GetForm();

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