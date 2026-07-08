////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list commands
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

// A list model is a command STORE (ibTabularCommandDataObject): GetCommandCollection lists its OWN set (the standard Add / Copy /
// Edit / Delete emitted inline, plus MarkAsDelete / AddFolder), CallAsCommand runs one by id on the FRONT-passed
// row. The TableBox merges the set into the real action and adds Select + the Filter / ViewMode band on the front.
// Each list class owns its command ids here (the base ibValueModel carries no shared command enum — every model
// defines its own). The standard Add / Copy / Edit / Delete + the list extras MarkAsDelete / AddFolder.
enum
{
	eAddValue = 1,
	eCopyValue,
	eEditValue = 3 | eStartEditingFlag,   // Edit's id carries the front-edit flag (baked in; the front reads the bit, opens the inline editor)
	eDeleteValue = 4,
	eMarkAsDelete = 26,
	eAddFolder = 27,
};

// (Enum list: no commands of its own — the base empty GetCommandCollection / CallAsCommand serve it.)

///////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListDataObjectRef::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	commands.emplace_back(eAddValue,    wxT("Add"),    _("Add"),    g_picAddCLSID,    true);
	commands.emplace_back(eCopyValue,   wxT("Copy"),   _("Copy"),   g_picCopyCLSID);
	commands.emplace_back(eEditValue,   wxT("Edit"),   _("Edit"),   g_picEditCLSID);
	commands.emplace_back(eDeleteValue, wxT("Delete"), _("Delete"), g_picDeleteCLSID);
	commands.emplace_back(eMarkAsDelete, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true);
}

void ibValueListDataObjectRef::CallAsCommand(const ibDataViewItem& row, const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eAddValue:     AddValue(row);       break;
		case eCopyValue:    CopyValue(row);      break;
		case eEditValue:    EditValue(row);      break;
		case eDeleteValue:  DeleteValue(row);    break;
		case eMarkAsDelete: MarkAsDeleteValue(row); break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueModelTreeDataObjectFolderRef::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	if (m_listMode == LIST_FOLDER || m_listMode == LIST_ITEM || m_listMode == LIST_ITEM_FOLDER)
		commands.emplace_back(eAddFolder, wxT("AddFolder"), _("Add folder"), g_picAddFolderCLSID, true);

	commands.emplace_back(eAddValue,    wxT("Add"),    _("Add"),    g_picAddCLSID,    true);
	commands.emplace_back(eCopyValue,   wxT("Copy"),   _("Copy"),   g_picCopyCLSID);
	commands.emplace_back(eEditValue,   wxT("Edit"),   _("Edit"),   g_picEditCLSID);
	commands.emplace_back(eDeleteValue, wxT("Delete"), _("Delete"), g_picDeleteCLSID);
	if (m_listMode == LIST_FOLDER)   // a folder-only list has no plain Add
		commands.erase(std::remove_if(commands.begin(), commands.end(),
			[](const ibCommandItem& c) { return c.m_act_id == eAddValue; }), commands.end());

	commands.emplace_back(eMarkAsDelete, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true);
}

void ibValueModelTreeDataObjectFolderRef::CallAsCommand(const ibDataViewItem& row, const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eAddFolder:    AddFolderValue(row); break;
		case eMarkAsDelete: MarkAsDeleteValue(row); break;
		case eAddValue:     AddValue(row);       break;
		case eCopyValue:    CopyValue(row);      break;
		case eEditValue:    EditValue(row);      break;
		case eDeleteValue:  DeleteValue(row);    break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListRegisterObject::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	if (UseStandartCommand()) {
		commands.emplace_back(eAddValue,    wxT("Add"),    _("Add"),    g_picAddCLSID,    true);
		commands.emplace_back(eCopyValue,   wxT("Copy"),   _("Copy"),   g_picCopyCLSID);
		commands.emplace_back(eEditValue,   wxT("Edit"),   _("Edit"),   g_picEditCLSID);
		commands.emplace_back(eDeleteValue, wxT("Delete"), _("Delete"), g_picDeleteCLSID);
	}
}

void ibValueListRegisterObject::CallAsCommand(const ibDataViewItem& row, const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
		case eAddValue:    AddValue(row);    break;
		case eCopyValue:   CopyValue(row);   break;
		case eEditValue:   EditValue(row);   break;
		case eDeleteValue: DeleteValue(row); break;
	}
}
