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
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueTableColumnCollection, CValue);
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueTableColumnCollection::IValueTableColumnInfo, CValue);
wxIMPLEMENT_ABSTRACT_CLASS(IValueModel::IValueTableReturnLine, CValue);

wxIMPLEMENT_ABSTRACT_CLASS(IValueTable, IValueModel);
wxIMPLEMENT_ABSTRACT_CLASS(IValueTree, IValueModel);

IValueModel::IValueModel()
	: CValue(eValueTypes::TYPE_VALUE), m_srcNotifier(NULL)
{
}

wxDataViewItem IValueModel::GetSelection() const
{
	if (m_srcNotifier == NULL) {
		return wxDataViewItem(NULL);
	}
	return m_srcNotifier->GetSelection();
}

void IValueModel::RowValueStartEdit(const wxDataViewItem& item, unsigned int col)
{
	if (m_srcNotifier == NULL) {
		return;
	}
	m_srcNotifier->StartEditing(item, col);
}

enum
{
	eAddValue = 1,
	eCopyValue,
	eEditValue,
	eDeleteValue
};

IValueModel::actionData_t IValueModel::GetActions(const form_identifier_t &formType)
{
	actionData_t action(this);
	action.AddAction("add", _("Add"), eAddValue);
	action.AddAction("copy", _("Copy"), eCopyValue);
	action.AddAction("edit", _("Edit"), eEditValue);
	action.AddAction("delete", _("Delete"), eDeleteValue);
	return action;
}

void IValueModel::ExecuteAction(const action_identifier_t &action, CValueForm* srcForm)
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

///////////////////////////////////////////////////////////////////////////////////////s

bool IValueModel::IValueTableColumnCollection::HasColumnID(unsigned int col_id)
{
	return GetColumnByID(col_id) != NULL;
}

///////////////////////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

IValueModel::IValueTableColumnCollection::IValueTableColumnInfo::IValueTableColumnInfo() : 
	CValue(eValueTypes::TYPE_VALUE, true), m_methods(new CMethods())
{
}

IValueModel::IValueTableColumnCollection::IValueTableColumnInfo::~IValueTableColumnInfo()
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

void IValueModel::IValueTableColumnCollection::IValueTableColumnInfo::PrepareNames() const
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

CValue IValueModel::IValueTableColumnCollection::IValueTableColumnInfo::GetAttribute(attributeArg_t& aParams)
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


IValueModel::IValueTableColumnCollection::IValueTableColumnInfo* IValueModel::IValueTableColumnCollection::GetColumnByID(unsigned int col_id)
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++)
	{
		IValueTableColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);

		if (col_id == columnInfo->GetColumnID())
			return columnInfo;
	}

	return NULL;
}

#include "utils/stringUtils.h"

IValueModel::IValueTableColumnCollection::IValueTableColumnInfo* IValueModel::IValueTableColumnCollection::GetColumnByName(const wxString& colName)
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++)
	{
		IValueTableColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (StringUtils::CompareString(colName, columnInfo->GetColumnName()))
			return columnInfo;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IValueModel::IValueTableReturnLine::IValueTableReturnLine() : CValue(eValueTypes::TYPE_VALUE, true) {}

IValueModel::IValueTableReturnLine::~IValueTableReturnLine() { }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "frontend/controls/dataView.h"

void CTableModelNotifier::Select(const wxDataViewItem& sel) const
{
	m_mainWindow->Select(sel);
}

wxDataViewItem CTableModelNotifier::GetSelection() const
{
	return m_mainWindow->GetSelection();
}

int CTableModelNotifier::GetSelections(wxDataViewItemArray& sel) const
{
	return m_mainWindow->GetSelections(sel);
}

void CTableModelNotifier::StartEditing(const wxDataViewItem& item, unsigned int col) const
{
	int viewColumn = m_mainWindow->GetModelColumnIndex(col);
	if (viewColumn != wxNOT_FOUND) {
		m_mainWindow->EditItem(item,
			m_mainWindow->GetColumn(viewColumn)
		);
	}
	else if (col == 0 && m_mainWindow->GetColumnCount() > 0) {
		m_mainWindow->EditItem(item,
			m_mainWindow->GetColumnAt(0)
		);
	}
}