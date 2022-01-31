#include "tableBox.h"
#include "metadata/objects/baseObject.h"
#include "form.h"

//****************************************************************************
//*                              actions                                     *
//****************************************************************************

CValueTableBox::actionData_t CValueTableBox::GetActions(form_identifier_t formType)
{
	if (!m_tableModel) {
		return actionData_t();
	}

	return m_tableModel->GetActions(formType);
}

#include "appData.h"

void CValueTableBox::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
	if (!m_tableModel)
		return;

	if (!appData->DesignerMode()) {
		m_tableModel->ExecuteAction(action, srcForm);
	}
}