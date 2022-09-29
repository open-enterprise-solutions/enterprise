////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - object
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"
#include "metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CObjectDataProcessor::CObjectDataProcessor(CMetaObjectDataProcessor *metaObject) :
	IRecordDataObjectExt(metaObject)
{
}

CObjectDataProcessor::CObjectDataProcessor(const CObjectDataProcessor& source) : 
	IRecordDataObjectExt(source)
{
}

void CObjectDataProcessor::ShowFormValue(const wxString &formName, IValueFrame *owner)
{
	CValueForm *foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm *valueForm = GetFormValue(formName, owner);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

CValueForm *CObjectDataProcessor::GetFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetForm();

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectDataProcessor::eFormDataProcessor);
	}

	CValueForm *valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
	}
	else {
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectDataProcessor::eFormDataProcessor);
	}

	return valueForm;
}