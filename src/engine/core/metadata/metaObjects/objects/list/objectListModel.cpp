////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list model 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void CListDataObjectEnumRef::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return;
	node->GetValue(col, variant);
}

bool CListDataObjectEnumRef::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return false;
	return node->SetValue(col, variant);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void CListDataObjectRef::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return;
	node->GetValue(col, variant);
}

bool CListDataObjectRef::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return false;
	return node->SetValue(col, variant);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& item, unsigned int col) const
{
	wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
	if (node == NULL)
		return;
	node->GetValue(col, variant);
}

bool CTreeDataObjectFolderRef::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& item, unsigned int col)
{
	wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
	if (node == NULL)
		return false;
	return node->SetValue(col, variant);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CTreeDataObjectFolderRef::GetAttrByRow(const wxDataViewItem& item,
	unsigned int col, wxDataViewItemAttr& attr) const
{
	wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
	if (node == NULL)
		return false;
	CValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
	if (isFolder.GetBoolean())
		attr.SetBackgroundColour(wxColour(224, 212, 190));
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return;
	node->GetValue(col, variant);
}

bool CListRegisterObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return false;
	return node->SetValue(col, variant);
}