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

CObjectCatalog::CObjectCatalog(const CObjectCatalog& source) :
	IRecordDataObjectRef(source), m_objMode(source.m_objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject(&source);
}

CObjectCatalog::CObjectCatalog(CMetaObjectCatalog* metaObject, int objMode) :
	IRecordDataObjectRef(metaObject), m_objMode(objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject();
}

CObjectCatalog::CObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& guid, int objMode) :
	IRecordDataObjectRef(metaObject, guid), m_objMode(objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject();
}

CSourceExplorer CObjectCatalog::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	CMetaObjectCatalog* metaRef = NULL;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetCatalogCode(), false);
		srcHelper.AppendSource(metaRef->GetCatalogDescription());
		CMetaDefaultAttributeObject* defOwner = metaRef->GetCatalogOwner();
		if (defOwner != NULL && defOwner->GetTypeCount() > 0) {
			srcHelper.AppendSource(metaRef->GetCatalogOwner());
		}
	}

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectCatalog::eFormObject);
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
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalog::eFormObject);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

void CObjectCatalog::FillObject(CValue& vFillObject)
{
	if (appData->EnterpriseMode()) {
		m_procUnit->CallFunction("Filling", vFillObject);
	}
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