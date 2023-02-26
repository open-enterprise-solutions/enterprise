#include "tableBox.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "form.h"

//****************************************************************************
//*                              actions                                     *
//****************************************************************************

CValueTableBox::actionData_t CValueTableBox::GetActions(const form_identifier_t& formType)
{
	if (m_tableModel == NULL) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (m_dataSource.isValid()) {
			if (srcObject != NULL) {
				IValueModel* tableModel = NULL;
				if (srcObject->GetModel(tableModel, GetIdByGuid(m_dataSource))) {
					return tableModel->GetActions(formType);
				}
			}
		}
		return actionData_t();
	}

	return m_tableModel->GetActions(formType);
}

#include "appData.h"

void CValueTableBox::ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm)
{
	if (m_tableModel == NULL)
		return;

	if (!appData->DesignerMode()) {
		m_tableModel->ExecuteAction(lNumAction, srcForm);
	}
}