////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value table and key pair 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"
#include "methods.h"
#include "functions.h"
#include "frontend/mainFrame.h"

#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable, IValueTable);

//////////////////////////////////////////////////////////////////////

CMethods CValueTable::m_methods;

wxDataViewItem CValueTable::FindRowValue(const CValue& cVal, const wxString& colName) const
{
	IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(colName);
	if (colInfo != NULL) {
		for (long row = 0; row < GetRowCount(); row++) {
			const wxDataViewItem& item = GetItem(row);
			wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
			if (node != NULL &&
				cVal == node->GetValue((meta_identifier_t)colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return wxDataViewItem(NULL);
}

wxDataViewItem CValueTable::FindRowValue(IValueModelReturnLine* retLine) const
{
	return wxDataViewItem(NULL);
}

CValueTable::CValueTable() : IValueTable()
{
	m_dataColumnCollection = new CValueTableColumnCollection(this);
	m_dataColumnCollection->IncrRef();
}

CValueTable::CValueTable(const CValueTable& valueTable) : IValueTable(),
m_dataColumnCollection(valueTable.m_dataColumnCollection)
{
	m_dataColumnCollection->IncrRef();
}

CValueTable::~CValueTable()
{
	if (m_dataColumnCollection)
		m_dataColumnCollection->DecrRef();
}

// methods:
enum
{
	enAddRow = 0,
	enClone,
	enCount,
	enFind,
	enDelete,
	enClear,
};

//attributes:
enum
{
	enColumns = 0,
};

void CValueTable::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"add","add()"},
		{"clone","clone()"},
		{"count","count()"},
		{"find","find(value, column)"},
		{"delete","delete(row)"},
		{"clear","clear()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);

	SEng aAttributes[] =
	{
		{"columns","columns"}
	};

	int nCountA = sizeof(aAttributes) / sizeof(aAttributes[0]);
	m_methods.PrepareAttributes(aAttributes, nCountA);
}

CValue CValueTable::GetAttribute(attributeArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enColumns: return m_dataColumnCollection;
	}

	return ret;
}

CValue CValueTable::Method(methodArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enAddRow: return AppendRow();
	case enClone: return Clone();
	case enFind: {
		const wxDataViewItem& item = FindRowValue(aParams[0], aParams[1].GetString());
		if (item.IsOk())
			return GetRowAt(item);
		break;
	}
	case enCount: return (unsigned int)GetRowCount();
	case enDelete: {
		CValueTableReturnLine* retLine = NULL;
		if (aParams[0].ConvertToValue(retLine)) {
			wxValueTableRow* node = GetViewData(retLine->GetLineItem());
			if (node != NULL)
				IValueTable::Remove(node);
		}
		else {
			number_t number = aParams[0].GetNumber();
			wxValueTableRow* node = GetViewData(GetItem(number.ToInt()));
			if (node != NULL)
				IValueTable::Remove(node);
		}
		break;
	}
	case enClear: 
		Clear();
		break;
	}

	return ret;
}

#include "appData.h"

CValue CValueTable::GetAt(const CValue& cKey)
{
	long index = cKey.ToUInt();
	if (index >= GetRowCount() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));
	return new CValueTableReturnLine(this, GetItem(index));
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnCollection                        //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection, IValueTable::IValueModelColumnCollection);

CValueTable::CValueTableColumnCollection::CValueTableColumnCollection(CValueTable* ownerTable) : IValueModelColumnCollection(),
m_methods(new CMethods()), m_ownerTable(ownerTable) {
}

CValueTable::CValueTableColumnCollection::~CValueTableColumnCollection() {
	for (auto& colInfo : m_columnInfo) {
		wxASSERT(colInfo);
		colInfo->DecrRef();
	}
	wxDELETE(m_methods);
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
enum
{
	enAddColumn = 0,
	enRemoveColumn
};

void CValueTable::CValueTableColumnCollection::PrepareNames() const
{
	std::vector<SEng> aMethods =
	{
		{"addColumn","add(name, type, caption, width)"},
		{"removeColumn","removeColumn(name)"},
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

#include "valueType.h"

CValue CValueTable::CValueTableColumnCollection::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enAddColumn: {
		CValueType* valueType = NULL;
		if (aParams.GetParamCount() > 1)
			aParams[1].ConvertToValue(valueType);
		if (aParams.GetParamCount() > 3)
			return AddColumn(aParams[0].ToString(), valueType ? new CValueTypeDescription(valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[2].ToString(), aParams[3].ToInt());
		else if (aParams.GetParamCount() > 2)
			return AddColumn(aParams[0].ToString(), valueType ? new CValueTypeDescription(valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[2].ToString(), wxDVC_DEFAULT_WIDTH);
		else if (aParams.GetParamCount() > 1)
			return AddColumn(aParams[0].ToString(), valueType ? new CValueTypeDescription(valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[0].ToString(), wxDVC_DEFAULT_WIDTH);
		else
			return AddColumn(aParams[0].ToString(), NULL, aParams[0].ToString(), wxDVC_DEFAULT_WIDTH);
		break;
	}
	case enRemoveColumn:
	{
		wxString columnName = aParams[0].ToString();
		auto itFounded = std::find_if(m_columnInfo.begin(), m_columnInfo.end(),
			[columnName](CValueTableColumnInfo* colData)
			{
				return StringUtils::CompareString(columnName, colData->GetColumnName());
			});
		if (itFounded != m_columnInfo.end()) {
			RemoveColumn((*itFounded)->GetColumnID());
		}
		break;
	}
	}

	return CValue();
}

void CValueTable::CValueTableColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue CValueTable::CValueTableColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	return *itFounded;
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnInfo                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo() : IValueModelColumnInfo() {
}

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo(unsigned int colID, const wxString& colName, CValueTypeDescription* types, const wxString& caption, int width) :
	IValueModelColumnInfo(), m_columnID(colID), m_columnName(colName), m_columnTypes(types), m_columnCaption(caption), m_columnWidth(width) {
	if (m_columnTypes) m_columnTypes->IncrRef();
}

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::~CValueTableColumnInfo() {
	if (m_columnTypes)
		m_columnTypes->DecrRef();
}

//////////////////////////////////////////////////////////////////////
//               CValueTableReturnLine                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableReturnLine, IValueTable::IValueModelReturnLine);

CValueTable::CValueTableReturnLine::CValueTableReturnLine(CValueTable* ownerTable, const wxDataViewItem& line) :
	IValueModelReturnLine(line), m_methods(new CMethods()), m_ownerTable(ownerTable) {
}

CValueTable::CValueTableReturnLine::~CValueTableReturnLine() {
	if (m_methods)
		delete m_methods;
}

void CValueTable::CValueTableReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	if (m_ownerTable->ValidateReturnLine(this)) {
		wxValueTableRow* node = m_ownerTable->GetViewData(m_lineItem);
		if (node == NULL)
			return;
		CValueTypeDescription* typeDescription =
			m_ownerTable->m_dataColumnCollection->GetColumnType(id);
		node->SetValue(typeDescription ? typeDescription->AdjustValue(cVal) : cVal, id, true);
	}
}

CValue CValueTable::CValueTableReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	CValueTableReturnLine* ret = const_cast<CValueTableReturnLine*>(this);
	wxASSERT(ret);
	if (m_ownerTable->ValidateReturnLine(ret)) {
		wxValueTableRow* node = m_ownerTable->GetViewData(m_lineItem);
		if (node == NULL)
			return CValue();
		return node->GetValue(id);
	}
	return _("Error while reading data");
}

void CValueTable::CValueTableReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;
	for (auto& colInfo : m_ownerTable->m_dataColumnCollection->m_columnInfo) {
		wxASSERT(colInfo);
		SEng aAttribute;
		aAttribute.sName = colInfo->GetColumnName();
		aAttribute.sSynonym = wxT("default");
		aAttribute.iName = colInfo->GetColumnID();
		aAttributes.push_back(aAttribute);
	}
	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueTable::CValueTableReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	if (appData->DesignerMode())
		return;

	SetValueByMetaID(
		m_methods->GetAttributePosition(aParams.GetIndex()),
		cVal
	);
}

CValue CValueTable::CValueTableReturnLine::GetAttribute(attributeArg_t& aParams)
{
	if (appData->DesignerMode())
		return CValue();

	return GetValueByMetaID(
		m_methods->GetAttributePosition(aParams.GetIndex())
	);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueTable, "table", g_valueTableCLSID);

SO_VALUE_REGISTER(CValueTable::CValueTableColumnCollection, "tableValueColumn", CValueTableColumnCollection, TEXT2CLSID("VL_TAVC"));
SO_VALUE_REGISTER(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, "tableValueColumnInfo", CValueTableColumnInfo, TEXT2CLSID("VL_TVCI"));
SO_VALUE_REGISTER(CValueTable::CValueTableReturnLine, "tableValueRow", CValueTableReturnLine, TEXT2CLSID("VL_TVCR"));