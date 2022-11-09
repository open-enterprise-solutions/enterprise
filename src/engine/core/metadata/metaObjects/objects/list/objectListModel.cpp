////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list model 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void CListDataObjectRef::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableListRow* node = GetViewData<wxValueTableListRow>(row);
	if (node == NULL)
		return;
	node->GetValue(variant, col);
}

bool CListDataObjectRef::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxValueTableListRow* node = GetViewData<wxValueTableListRow>(row);
	if (node == NULL)
		return false;
	return node->SetValue(variant, col);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& item, unsigned int col) const
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(item);
	if (node == NULL)
		return;
	node->GetValue(variant, col);
}

bool CTreeDataObjectFolderRef::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& item, unsigned int col)
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(item);
	if (node == NULL)
		return false;
	return node->SetValue(variant, col);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CTreeDataObjectFolderRef::GetAttrByRow(const wxDataViewItem& item, 
	unsigned int col, wxDataViewItemAttr& attr) const
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(item);
	if (node == NULL)
		return false;
	CValue isFolder = false; 
	node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
	if (isFolder.GetBoolean()) {
		attr.SetBackgroundColour(wxColour(224, 212, 190));
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return;
	node->GetValue(variant, col);
}

bool CListRegisterObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return false;
	return node->SetValue(variant, col);
}