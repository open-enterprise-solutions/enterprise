////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
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

CObjectDocumentValue::CObjectDocumentValue(const CObjectDocumentValue &source) :
	IDataObjectRefValue(source)
{
	InitializeObject(&source);
}

CObjectDocumentValue::CObjectDocumentValue(CMetaObjectDocumentValue *metaObject) :
	IDataObjectRefValue(metaObject)
{
	InitializeObject();
}

CObjectDocumentValue::CObjectDocumentValue(CMetaObjectDocumentValue *metaObject, const Guid &guid) :
	IDataObjectRefValue(metaObject, guid)
{
	InitializeObject();
}

void CObjectDocumentValue::ShowFormValue(const wxString &formName, IValueFrame *owner)
{
	CValueForm *foundedForm = GetFrame();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm *valueForm = GetFormValue(formName, owner);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

CValueForm *CObjectDocumentValue::GetFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetFrame();

	if (foundedForm)
		return foundedForm;

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectDocumentValue::eFormObject);
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
		valueForm->BuildForm(CMetaObjectDocumentValue::eFormObject);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Document events                                            *
//***********************************************************************************************

void CObjectDocumentValue::FillObject(CValue &vFillObject)
{
	if (appData->EnterpriseMode()) {
		m_procUnit->CallFunction("Filling", vFillObject);
	}
}

CValue CObjectDocumentValue::CopyObject()
{
	if (appData->EnterpriseMode()) {
		return CopyObjectValue();
	}

	return CValue();
}

bool CObjectDocumentValue::WriteObject()
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

bool CObjectDocumentValue::DeleteObject()
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