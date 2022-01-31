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

CObjectCatalogValue::CObjectCatalogValue(const CObjectCatalogValue &source) :
	IDataObjectRefValue(source), m_objMode(source.m_objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject(&source);
}

CObjectCatalogValue::CObjectCatalogValue(CMetaObjectCatalogValue *metaObject, int objMode) :
	IDataObjectRefValue(metaObject), m_objMode(objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject();
}

CObjectCatalogValue::CObjectCatalogValue(CMetaObjectCatalogValue *metaObject, const Guid &guid, int objMode) :
	IDataObjectRefValue(metaObject, guid), m_objMode(objMode), m_catOwner(NULL), m_catParent(NULL)
{
	InitializeObject();
}

void CObjectCatalogValue::ShowFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetFrame();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm *valueForm =
		GetFormValue(formName, ownerControl);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

CValueForm *CObjectCatalogValue::GetFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetFrame();

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectCatalogValue::eFormObject);
	}

	CValueForm *valueForm = NULL;

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
		valueForm->BuildForm(CMetaObjectCatalogValue::eFormObject);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

void CObjectCatalogValue::FillObject(CValue &vFillObject)
{
	if (appData->EnterpriseMode()) {
		m_procUnit->CallFunction("Filling", vFillObject);
	}
}

CValue CObjectCatalogValue::CopyObject()
{
	if (appData->EnterpriseMode()) {
		return CopyObjectValue();
	}

	return CValue();
}

bool CObjectCatalogValue::WriteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
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

			if (!SaveInDB()) {
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

			CValueForm *valueForm = GetFrame();

			if (valueForm) {
				valueForm->UpdateForm();
				valueForm->Modify(false);
			}

			m_objModified = false;
		}
	}

	return true;
}

bool CObjectCatalogValue::DeleteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			databaseLayer->BeginTransaction();

			{
				CValue cancel = false;
				m_procUnit->CallFunction("BeforeDelete", cancel);
				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return false;
				}
			}

			if (!DeleteInDB()) {
				databaseLayer->RollBack(); CSystemObjects::Raise("failed to delete object in db!"); return false;
			}

			{
				CValue cancel = false;
				m_procUnit->CallFunction("OnDelete", cancel);
				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return false;
				}
			}

			databaseLayer->Commit();
		}
	}

	return true;
}