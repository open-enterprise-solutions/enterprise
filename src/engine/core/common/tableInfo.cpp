////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table model information
////////////////////////////////////////////////////////////////////////////

#include "tableInfo.h"

static int my_sort(int* v1, int* v2)
{
	return *v2 - *v1;
}

wxIMPLEMENT_ABSTRACT_CLASS(IValueModel, CValue);
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueModelColumnCollection, CValue);
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueModelColumnCollection::IValueModelColumnInfo, CValue);
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueModelReturnLine, CValue);

wxIMPLEMENT_ABSTRACT_CLASS(IValueTable, IValueModel);
wxIMPLEMENT_ABSTRACT_CLASS(IValueTree, IValueModel);

IValueModel::IValueModel()
	: CValue(eValueTypes::TYPE_VALUE), m_srcNotifier(NULL)
{
}

wxDataViewItem IValueModel::GetSelection() const
{
	if (m_srcNotifier == NULL)
		return wxDataViewItem(NULL);
	return m_srcNotifier->GetSelection();
}

void IValueModel::RowValueStartEdit(const wxDataViewItem& item, unsigned int col)
{
	if (m_srcNotifier == NULL)
		return;
	m_srcNotifier->StartEditing(item, col);
}

IValueModel::actionData_t IValueModel::GetActions(const form_identifier_t& formType)
{
	actionData_t action(this);
	action.AddAction("add", _("Add"), eAddValue);
	action.AddAction("copy", _("Copy"), eCopyValue);
	action.AddAction("edit", _("Edit"), eEditValue);
	action.AddAction("delete", _("Delete"), eDeleteValue);
	return action;
}

void IValueModel::ExecuteAction(const action_identifier_t& action, CValueForm* srcForm)
{
	switch (action)
	{
	case eAddValue:
		AddValue();
		break;
	case eCopyValue:
		CopyValue();
		break;
	case eEditValue:
		EditValue();
		break;
	case eDeleteValue:
		DeleteValue();
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

IValueModel::IValueModelColumnCollection::IValueModelColumnInfo::IValueModelColumnInfo() :
	CValue(eValueTypes::TYPE_VALUE, true), m_methods(new CMethods())
{
}

IValueModel::IValueModelColumnCollection::IValueModelColumnInfo::~IValueModelColumnInfo()
{
	wxDELETE(m_methods);
}

enum
{
	enColumnName,
	enColumnTypes,
	enColumnCaption,
	enColumnWidth
};

void IValueModel::IValueModelColumnCollection::IValueModelColumnInfo::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	{
		SEng aAttribute;
		aAttribute.sName = wxT("name");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("types");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("caption");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("width");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue IValueModel::IValueModelColumnCollection::IValueModelColumnInfo::GetAttribute(attributeArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enColumnName:
		return GetColumnName();
	case enColumnTypes:
		return GetColumnTypes();
	case enColumnCaption:
		return GetColumnCaption();
	case enColumnWidth:
		return GetColumnWidth();
	}

	return CValue();
}


IValueModel::IValueModelColumnCollection::IValueModelColumnInfo* IValueModel::IValueModelColumnCollection::GetColumnByID(unsigned int col) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		IValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (col == columnInfo->GetColumnID())
			return columnInfo;
	}

	return NULL;
}

#include "utils/stringUtils.h"

IValueModel::IValueModelColumnCollection::IValueModelColumnInfo* IValueModel::IValueModelColumnCollection::GetColumnByName(const wxString& colName) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		IValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (StringUtils::CompareString(colName, columnInfo->GetColumnName()))
			return columnInfo;
	}

	return NULL;
}