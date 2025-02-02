////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list actionData 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

enum {
	eChooseValue = 25,
	eAddFolder = 26,
};

CListDataObjectEnumRef::actionData_t CListDataObjectEnumRef::GetActions(const form_identifier_t& formType)
{
	actionData_t actionData(this);

	if (m_choiceMode)
		actionData.AddAction("select", _("Select"), eChooseValue);

	const actionData_t& data =
		IValueTable::GetActions(formType);

	for (unsigned int idx = 0; idx < data.GetCount(); idx++) {
		const action_identifier_t& id = data.GetID(idx);
		if (id > 0) {
			actionData.AddAction(
				data.GetNameByID(id),
				data.GetCaptionByID(id),
				id
			);
		}
		else {
			actionData.AddSeparator();
		}
	}

	return actionData;
}

void CListDataObjectEnumRef::ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eChooseValue:
		ChooseValue(srcForm);
		break;
	default:
		IValueTable::ExecuteAction(lNumAction, srcForm);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CListDataObjectRef::actionData_t CListDataObjectRef::GetActions(const form_identifier_t& formType)
{
	actionData_t actionData(this);

	if (m_choiceMode)
		actionData.AddAction("select", _("Select"), eChooseValue);

	if (m_choiceMode)
		actionData.AddSeparator();

	const actionData_t& data =
		IValueTable::GetActions(formType);

	for (unsigned int idx = 0; idx < data.GetCount(); idx++) {
		const action_identifier_t& id = data.GetID(idx);
		if (id > 0) {
			actionData.AddAction(
				data.GetNameByID(id),
				data.GetCaptionByID(id),
				id
			);
		}
		else {
			actionData.AddSeparator();
		}
	}

	return actionData;
}

void CListDataObjectRef::ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eChooseValue:
		ChooseValue(srcForm);
		break;
	default:
		IValueTable::ExecuteAction(lNumAction, srcForm);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CListDataObjectRef::actionData_t CTreeDataObjectFolderRef::GetActions(const form_identifier_t& formType)
{
	actionData_t actionData(this);

	if (m_choiceMode) {
		actionData.AddAction("select", _("Select"), eChooseValue);
		actionData.AddSeparator();
	}

	if (m_listMode == LIST_FOLDER || m_listMode == LIST_ITEM || m_listMode == LIST_ITEM_FOLDER) {
		actionData.AddAction("addFolder", _("Add folder"), eAddFolder);
	}

	actionData_t data =
		IValueTree::GetActions(formType);

	if (m_listMode == LIST_FOLDER)
		data.RemoveAction(eAddValue); //add

	for (unsigned int idx = 0; idx < data.GetCount(); idx++) {
		const action_identifier_t& id = data.GetID(idx);
		if (id > 0) {
			actionData.AddAction(
				data.GetNameByID(id),
				data.GetCaptionByID(id),
				id
			);
		}
		else {
			actionData.AddSeparator();
		}
	}

	return actionData;
}

void CTreeDataObjectFolderRef::ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eAddFolder:
		AddFolderValue();
		break;
	case eChooseValue:
		ChooseValue(srcForm);
		break;
	default:
		IValueTree::ExecuteAction(lNumAction, srcForm);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CListRegisterObject::actionData_t CListRegisterObject::GetActions(const form_identifier_t& formType)
{
	if (!m_metaObject->HasRecorder()) {
		return IValueTable::GetActions(formType);
	}

	return actionData_t(this);
}

void CListRegisterObject::ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm)
{
	if (!m_metaObject->HasRecorder()) {
		IValueTable::ExecuteAction(lNumAction, srcForm);
	}
}