////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list model 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ibValueListDataObjectEnumRef::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool ibValueListDataObjectEnumRef::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	return node->SetValue(col, variant);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListDataObjectRef::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool ibValueListDataObjectRef::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	return node->SetValue(col, variant);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueModelTreeDataObjectFolderRef::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& item, unsigned int col) const
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool ibValueModelTreeDataObjectFolderRef::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& item, unsigned int col)
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	if (node == nullptr)
		return false;
	return node->SetValue(col, variant);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueModelTreeDataObjectFolderRef::GetAttrByRow(const ibDataViewItem& item,
	unsigned int col, ibDataViewItemAttr& attr) const
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	if (node == nullptr)
		return false;
	// A GROUP node (grouping replaces the folder tree) carries ONLY its dimension value — it has
	// NO IsFolder cell. GetTableValue is m_nodeValues.at(), which THROWS std::out_of_range on a missing key; that
	// threw here per group ROW during paint → the folder-highlight probe aborted the row render, leaving the
	// group headers blank and glitchy. Use the safe GetValue (returns false on a missing cell) so a group node
	// simply gets no folder background.
	ibValue isFolder;
	if (node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder) && isFolder.GetBoolean())
		attr.SetBackgroundColour(wxColour(214, 239, 252));
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListRegisterObject::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool ibValueListRegisterObject::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	return node->SetValue(col, variant);
}