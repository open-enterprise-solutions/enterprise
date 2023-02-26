////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - object
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "core/metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/compiler/systemObjects.h"

#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CObjectReport::CObjectReport(CMetaObjectReport* metaObject) : IRecordDataObjectExt(metaObject)
{
}

CObjectReport::CObjectReport(const CObjectReport& source) : IRecordDataObjectExt(source)
{
}

void CObjectReport::ShowFormValue(const wxString& formName, IControlFrame* owner)
{
	CValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm =
		GetFormValue(formName, owner);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

CValueForm* CObjectReport::GetFormValue(const wxString& formName, IControlFrame* ownerControl)
{
	CValueForm* const foundedForm = GetForm();

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectReport::eFormReport);
	}

	CValueForm* valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
	}
	else {
		valueForm = new CValueForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectReport::eFormReport);
	}

	return valueForm;
}