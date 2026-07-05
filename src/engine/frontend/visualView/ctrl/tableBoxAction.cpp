#include "tableBox.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "form.h"

//****************************************************************************
//*                              actionData                                     *
//****************************************************************************

ibValueModelTableBox::ibActionCollection ibValueModelTableBox::GetActionCollection(const ibFormID& formType)
{
	if (m_tableModel == nullptr) {

		ibValuePtr<ibValueModel> pvarControlVal;
		if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
			m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal)) {
			return pvarControlVal->GetActionCollection(formType);
		}

		return ibActionCollection();
	}

	return m_tableModel->GetActionCollection(formType);
}

#include "backend/appData.h"

void ibValueModelTableBox::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	if (m_tableModel == nullptr) return;
	if (!appData->DesignerMode()) {
		m_tableModel->ExecuteAction(lNumAction, srcForm);
	}
}