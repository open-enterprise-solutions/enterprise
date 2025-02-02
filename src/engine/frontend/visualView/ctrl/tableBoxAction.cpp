#include "tableBox.h"
#include "backend/metaCollection/partial/object.h"
#include "form.h"

//****************************************************************************
//*                              actionData                                     *
//****************************************************************************

CValueTableBox::actionData_t CValueTableBox::GetActions(const form_identifier_t& formType)
{
	if (m_tableModel == nullptr) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (m_dataSource.isValid()) {
			if (srcObject != nullptr) {
				IValueModel* tableModel = nullptr;
				if (srcObject->GetModel(tableModel, GetIdByGuid(m_dataSource))) {
					return tableModel->GetActions(formType);
				}
			}
		}
		return actionData_t();
	}

	return m_tableModel->GetActions(formType);
}

#include "backend/appData.h"

void CValueTableBox::ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm)
{
	if (m_tableModel == nullptr) return;
	if (!appData->DesignerMode()) {
		m_tableModel->ExecuteAction(lNumAction, srcForm);
	}
}