#include "tableBox.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "form.h"

//****************************************************************************
//*                              actions                                     *
//****************************************************************************

CValueTableBox::actionData_t CValueTableBox::GetActions(const form_identifier_t& formType)
{
	if (m_tableModel == NULL) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (m_dataSource != wxNOT_FOUND) {
			if (srcObject != NULL) {
				IValueTable* tableModel = NULL;
				if (srcObject->GetTable(tableModel, m_dataSource)) {
					return tableModel->GetActions(formType);
				}
			}
		}
		return actionData_t();
	}

	return m_tableModel->GetActions(formType);
}

#include "appData.h"

void CValueTableBox::ExecuteAction(const action_identifier_t& action, CValueForm* srcForm)
{
	if (m_tableModel == NULL)
		return;

	if (!appData->DesignerMode()) {
		m_tableModel->ExecuteAction(action, srcForm);
	}
}