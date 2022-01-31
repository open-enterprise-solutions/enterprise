////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list actions 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

enum
{
	eChooseValue = 6,
	eAddValue,
	eCopyValue,
	eEditValue,
	eDeleteValue
};

CDataObjectList::actionData_t CDataObjectList::GetActions(form_identifier_t formType)
{
	actionData_t aActions(this);
	if (m_bChoiceMode) {
		aActions.AddAction("select", eChooseValue);
	}
	if (!m_metaObject->isEnumeration()) {
		aActions.AddAction("add", eAddValue);
		aActions.AddAction("copy", eCopyValue);
		aActions.AddAction("edit", eEditValue);
		aActions.AddAction("delete", eDeleteValue);
	}
	return aActions;
}

void CDataObjectList::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
	switch (action)
	{
	case eChooseValue: ChooseValue(srcForm); break;
	case eAddValue:    AddValue();    break;
	case eCopyValue:   CopyValue();   break;
	case eEditValue:   EditValue();   break;
	case eDeleteValue: DeleteValue(); break;
	}
}
