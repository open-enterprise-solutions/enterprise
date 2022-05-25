////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list actions 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

enum
{
	eChooseValue = 25,
};

CListDataObjectRef::actionData_t CListDataObjectRef::GetActions(const form_identifier_t &formType)
{
	actionData_t actions(this);
	
	if (m_choiceMode) {
		actions.AddAction("select"	, _("Select"), eChooseValue);
	}
	
	if (m_metaObject->GetClsid() != g_metaEnumerationCLSID) {

		actionData_t data = 	
			IValueTable::GetActions(formType);

		for (unsigned int idx = 0; idx < data.GetCount(); idx++) {
			action_identifier_t id = data.GetID(idx);
			actions.AddAction(
				data.GetNameByID(id),
				data.GetCaptionByID(id),
				id
			);
		}
	}
	
	return actions;
}

void CListDataObjectRef::ExecuteAction(const action_identifier_t &action, CValueForm* srcForm)
{
	switch (action)
	{
	case eChooseValue: 
		ChooseValue(srcForm);
		break;
	default: 
		IValueTable::ExecuteAction(action, srcForm);
	}
}

CListRegisterObject::actionData_t CListRegisterObject::GetActions(const form_identifier_t &formType)
{
	if (!m_metaObject->HasRecorder()) {
		return IValueTable::GetActions(formType);
	}

	return actionData_t(this); 
}

void CListRegisterObject::ExecuteAction(const action_identifier_t &action, CValueForm* srcForm)
{
	if (!m_metaObject->HasRecorder()) {
		IValueTable::ExecuteAction(action, srcForm);
	}
}