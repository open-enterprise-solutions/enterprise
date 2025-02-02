////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - object
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
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

CRecordDataObjectReport::CRecordDataObjectReport(CMetaObjectReport* metaObject) : IRecordDataObjectExt(metaObject)
{
}

CRecordDataObjectReport::CRecordDataObjectReport(const CRecordDataObjectReport& source) : IRecordDataObjectExt(source)
{
}

void CRecordDataObjectReport::ShowFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	IBackendValueForm* valueForm =
		GetFormValue(formName, ownerControl);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

IBackendValueForm* CRecordDataObjectReport::GetFormValue(const wxString& formName, IBackendControlFrame* ownerControl)
{
	IBackendValueForm* const foundedForm = GetForm();

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectReport::eFormReport);
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
		valueForm->BuildForm(CMetaObjectReport::eFormReport);
	}

	return valueForm;
}