////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list actionData 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

enum
{
	eChooseValue = 25,
	eMarkAsDelete = 26,
	eAddFolder = 27,
};

ibValueListDataObjectEnumRef::ibActionCollection ibValueListDataObjectEnumRef::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection actionData(this);

	if (m_choiceMode)
		actionData.AddAction(wxT("Select"), _("Select"), g_picSelectCLSID, true, eChooseValue);

	actionData.AppendFrom(ibValueModel::GetActionCollection(formType));

	return actionData;
}

void ibValueListDataObjectEnumRef::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eChooseValue:
			ChooseValue(srcForm);
			break;
		default:
			ibValueModel::ExecuteAction(lNumAction, srcForm);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

ibValueListDataObjectRef::ibActionCollection ibValueListDataObjectRef::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection actionData(this);

	if (m_choiceMode)
		actionData.AddAction(wxT("Select"), _("Select"), g_picSelectCLSID, true, eChooseValue);

	if (m_choiceMode)
		actionData.AddSeparator();

	actionData.AppendFrom(ibValueModel::GetActionCollection(formType));

	actionData.InsertAction(3, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true, eMarkAsDelete);

	return actionData;
}

void ibValueListDataObjectRef::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eMarkAsDelete:
			MarkAsDeleteValue();
			break;
		case eChooseValue:
			ChooseValue(srcForm);
			break;
		default:
			ibValueModel::ExecuteAction(lNumAction, srcForm);
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

ibValueListDataObjectRef::ibActionCollection ibValueModelTreeDataObjectFolderRef::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection actionData(this);

	if (m_choiceMode) {
		actionData.AddAction(wxT("Select"), _("Select"), g_picSelectCLSID, true, eChooseValue);
		actionData.AddSeparator();
	}

	if (m_listMode == LIST_FOLDER || m_listMode == LIST_ITEM || m_listMode == LIST_ITEM_FOLDER) {
		actionData.AddAction(wxT("AddFolder"), _("Add folder"), g_picAddFolderCLSID, true, eAddFolder);
	}

	ibActionCollection data =
		ibValueModelCursor::GetActionCollection(formType);

	if (m_listMode == LIST_FOLDER)
		data.RemoveAction(eAddValue); //add

	actionData.AppendFrom(data);

	if (m_choiceMode) {
		actionData.InsertAction(5, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true, eMarkAsDelete);
	}
	else {
		actionData.InsertAction(4, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true, eMarkAsDelete);
	}

	return actionData;
}

void ibValueModelTreeDataObjectFolderRef::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eAddFolder:
			AddFolderValue();
			break;
		case eMarkAsDelete:
			MarkAsDeleteValue();
			break;
		case eChooseValue:
			ChooseValue(srcForm);
			break;
		default:
			ibValueModelCursor::ExecuteAction(lNumAction, srcForm);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

ibValueListRegisterObject::ibActionCollection ibValueListRegisterObject::GetActionCollection(const ibFormID& formType)
{
	return ibValueModel::GetActionCollection(formType);
}

void ibValueListRegisterObject::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	ibValueModel::ExecuteAction(lNumAction, srcForm);
}