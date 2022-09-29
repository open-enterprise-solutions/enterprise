#include "informationRegister.h"

#include "appData.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

#include "utils/stringUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "common/docManager.h"

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
		m_metaObject, GetClassType(),
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

void CRecordManagerInformationRegister::ShowFormValue(const wxString& formName, IValueFrame* owner)
{
	CValueForm* foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm = GetFormValue(formName, owner);

	valueForm->Modify(m_recordSet->IsModified());
	valueForm->ShowForm();
}

CValueForm* CRecordManagerInformationRegister::GetFormValue(const wxString& formName, IValueFrame* ownerControl)
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
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormRecord);
		valueForm->Modify(m_recordSet->IsModified());
	}

	return valueForm;
}

#include "appData.h"

CValue CRecordManagerInformationRegister::CopyRegister()
{
	if (appData->EnterpriseMode()) {
		return CopyRegisterValue();
	}

	return CValue();
}

#include "common/docManager.h"
#include "compiler/systemObjects.h"

bool CRecordManagerInformationRegister::WriteRegister(bool replace)
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* valueForm = GetForm();

			{
				if (!SaveData()) {
					 CSystemObjects::Raise(_("failed to delete object in db!"));
					return false;
				}

				CValueForm::UpdateFormKey(m_objGuid); 

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
				}

				if (valueForm) {
					valueForm->UpdateForm();
					valueForm->Modify(false);
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
			CValueForm* valueForm = GetForm();

			{
				if (!DeleteData()) {
					CSystemObjects::Raise(_("failed to delete object in db!"));
					return false;
				}

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