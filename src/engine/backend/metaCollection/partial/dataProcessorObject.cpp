////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - object
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"
#include "backend/metaData.h"

#include "backend/appData.h"
#include "reference/reference.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/systemManager/systemManager.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include "backend/fileSystem/fs.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CRecordDataObjectDataProcessor::CRecordDataObjectDataProcessor(CMetaObjectDataProcessor* metaObject) :
	IRecordDataObjectExt(metaObject)
{
}

CRecordDataObjectDataProcessor::CRecordDataObjectDataProcessor(const CRecordDataObjectDataProcessor& source) :
	IRecordDataObjectExt(source)
{
}

void CRecordDataObjectDataProcessor::ShowFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	IBackendValueForm* valueForm = GetFormValue(formName, ownerControl);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

IBackendValueForm* CRecordDataObjectDataProcessor::GetFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* foundedForm = GetForm();

	if (foundedForm)
		return foundedForm;

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectDataProcessor::eFormDataProcessor);
	}

	IBackendValueForm* valueForm = nullptr;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
	}
	else {
		valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectDataProcessor::eFormDataProcessor);
	}

	return valueForm;
}