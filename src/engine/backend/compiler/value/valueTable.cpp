////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value table and key pair 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"
#include "backend/backend_exception.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable, IValueTable);

//////////////////////////////////////////////////////////////////////

CValue::CMethodHelper CValueTable::m_methodHelper;

wxDataViewItem CValueTable::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(colName);
	if (colInfo != nullptr) {
		for (long row = 0; row < GetRowCount(); row++) {
			const wxDataViewItem& item = GetItem(row);
			wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
			if (node != nullptr &&
				varValue == node->GetTableValue((meta_identifier_t)colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return wxDataViewItem(nullptr);
}

wxDataViewItem CValueTable::FindRowValue(IValueModelReturnLine* retLine) const
{
	return wxDataViewItem(nullptr);
}

CValueTable::CValueTable() : IValueTable()
{
	m_dataColumnCollection = CValue::CreateAndConvertObjectValueRef<CValueTableColumnCollection>(this);
	m_dataColumnCollection->IncrRef();
}

CValueTable::CValueTable(const CValueTable& valueTable) : IValueTable(),
m_dataColumnCollection(valueTable.m_dataColumnCollection)
{
	m_dataColumnCollection->IncrRef();
}

CValueTable::~CValueTable()
{
	if (m_dataColumnCollection != nullptr)
		m_dataColumnCollection->DecrRef();
}

void CValueTable::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendFunc("add", "add()");
	m_methodHelper.AppendFunc("clone", "clone()");
	m_methodHelper.AppendFunc("count", "count()");
	m_methodHelper.AppendFunc("find", 2, "find(value, column)");
	m_methodHelper.AppendFunc("delete", 1, "delete(row)");
	m_methodHelper.AppendFunc("clear", "clear()");
	m_methodHelper.AppendFunc("sort", 2, "sort(column, ascending = true)");

	m_methodHelper.AppendProp("columns");

	if (m_dataColumnCollection != nullptr)
		m_dataColumnCollection->PrepareNames();
}

bool CValueTable::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColumns:
		pvarPropVal = m_dataColumnCollection;
		return true;
	}

	return false;
}

bool CValueTable::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAddRow:
		pvarRetValue = GetRowAt(AppendRow());
		return true;
	case enClone:
		pvarRetValue = Clone();
		return true;
	case enFind: {
		const wxDataViewItem& item = FindRowValue(*paParams[0], paParams[1]->GetString());
		if (item.IsOk())
			pvarRetValue = GetRowAt(item);
		return true;
	}
	case enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case enDelete: {
		CValueTableReturnLine* retLine = nullptr;
		if (paParams[0]->ConvertToValue(retLine)) {
			wxValueTableRow* node = GetViewData<wxValueTableRow>(retLine->GetLineItem());
			if (node != nullptr)
				IValueTable::Remove(node);
		}
		else {
			wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(paParams[0]->GetInteger()));
			if (node != nullptr) IValueTable::Remove(node);
		}
		return true;
	}
	case enClear:
		Clear();
		return true;
	case enSort:
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(paParams[0]->GetString());
		if (colInfo != nullptr) {
			IValueTable::Sort(colInfo->GetColumnID(), lSizeArray > 0 ? paParams[1]->GetBoolean() : true);
			return true;
		}
		return false;
	}

	return false;
}

#include "backend/appData.h"

bool CValueTable::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	const long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		CBackendException::Error("Array index out of bounds");
		return false;
	}
	pvarValue = CValue::CreateAndConvertObjectValueRef<CValueTableReturnLine>(this, GetItem(index));
	return true;
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnCollection                        //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection, IValueTable::IValueModelColumnCollection);

CValueTable::CValueTableColumnCollection::CValueTableColumnCollection(CValueTable* ownerTable) : IValueModelColumnCollection(),
m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable) {
}

CValueTable::CValueTableColumnCollection::~CValueTableColumnCollection() {
	for (auto& colInfo : m_columnInfo) {
		wxASSERT(colInfo);
		colInfo->DecrRef();
	}
	wxDELETE(m_methodHelper);
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
void CValueTable::CValueTableColumnCollection::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("addColumn"), 4, "add(name, type, caption, width)");
	m_methodHelper->AppendProc(wxT("removeColumn"), 1, "removeColumn(name)");
}

#include "valueType.h"

bool CValueTable::CValueTableColumnCollection::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRemoveColumn:
	{
		wxString columnName = paParams[0]->GetString();
		auto it = std::find_if(m_columnInfo.begin(), m_columnInfo.end(),
			[columnName](CValueTableColumnInfo* colData) {
				return stringUtils::CompareString(columnName, colData->GetColumnName());
			});
		if (it != m_columnInfo.end()) {
			RemoveColumn((*it)->GetColumnID());
		}
		return true;
	}
	}
	return false;
}

bool CValueTable::CValueTableColumnCollection::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAddColumn: {
		CValueType* valueType = nullptr;
		if (lSizeArray > 1)
			paParams[1]->ConvertToValue(valueType);
		if (lSizeArray > 3)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? valueType->GetOwnerTypeDescription() : *paParams[1]->ConvertToType<CValueTypeDescription>(), paParams[2]->GetString(), paParams[3]->GetInteger());
		else if (lSizeArray > 2)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? valueType->GetOwnerTypeDescription() : *paParams[1]->ConvertToType<CValueTypeDescription>(), paParams[2]->GetString(), wxDVC_DEFAULT_WIDTH);
		else if (lSizeArray > 1)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? valueType->GetOwnerTypeDescription() : *paParams[1]->ConvertToType<CValueTypeDescription>(), paParams[0]->GetString(), wxDVC_DEFAULT_WIDTH);
		else
			pvarRetValue = AddColumn(paParams[0]->GetString(), typeDescription_t(), paParams[0]->GetString(), wxDVC_DEFAULT_WIDTH);
		return true;
	}
	}

	return false;
}

bool CValueTable::CValueTableColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool CValueTable::CValueTableColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode())) {
		CBackendException::Error("Index goes beyond array"); return false;
	}
	auto it = m_columnInfo.begin();
	std::advance(it, index);
	pvarValue = *it;
	return true;
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnInfo                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo() : IValueModelColumnInfo() {
}

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo(unsigned int colID, const wxString& colName, const typeDescription_t& typeDescription, const wxString& caption, int width) :
	IValueModelColumnInfo(), m_columnID(colID), m_columnName(colName), m_columnType(typeDescription), m_columnCaption(caption), m_columnWidth(width) {
}

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::~CValueTableColumnInfo() {
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////
//               CValueTableReturnLine                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableReturnLine, IValueTable::IValueModelReturnLine);

CValueTable::CValueTableReturnLine::CValueTableReturnLine(CValueTable* ownerTable, const wxDataViewItem& line) :
	IValueModelReturnLine(line), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable) {
}

CValueTable::CValueTableReturnLine::~CValueTableReturnLine() {
	wxDELETE(m_methodHelper);
}

void CValueTable::CValueTableReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	for (auto& colInfo : m_ownerTable->m_dataColumnCollection->m_columnInfo) {
		wxASSERT(colInfo);
		m_methodHelper->AppendProp(
			colInfo->GetColumnName(),
			colInfo->GetColumnID()
		);
	}
}

bool CValueTable::CValueTableReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	if (appData->DesignerMode())
		return false;
	return SetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum),
		varPropVal
	);
}

bool CValueTable::CValueTableReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;

	return GetValueByMetaID(
		m_methodHelper->GetPropData(lPropNum), pvarPropVal
	);
}

//**********************************************************************

long CValueTable::AppendRow(unsigned int before)
{
	wxValueTableRow* rowData = new wxValueTableRow();
	for (auto& colData : m_dataColumnCollection->m_columnInfo) {
		rowData->AppendTableValue(colData->GetColumnID(),
			CValueTypeDescription::AdjustValue(m_dataColumnCollection->GetColumnType(colData->GetColumnID()))
		);
	}

	return IValueTable::Append(rowData, !CBackendException::IsEvalMode());
}

void CValueTable::EditRow()
{
	IValueTable::RowValueStartEdit(GetSelection());
}

void CValueTable::CopyRow()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData<wxValueTableRow>(currentItem);
	if (node == nullptr)
		return;
	wxValueTableRow* rowData = new wxValueTableRow();
	for (auto& colData : m_dataColumnCollection->m_columnInfo) {
		rowData->AppendTableValue(
			colData->GetColumnID(), node->GetTableValue(colData->GetColumnID())
		);
	}
	const long& currentLine = GetRow(currentItem);
	if (currentLine != wxNOT_FOUND) {
		IValueTable::Insert(rowData, currentLine, !CBackendException::IsEvalMode());
	}
	else {
		IValueTable::Append(rowData, !CBackendException::IsEvalMode());
	}
}

void CValueTable::DeleteRow()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData<wxValueTableRow>(currentItem);
	if (node == nullptr)
		return;
	if (!CBackendException::IsEvalMode())
		IValueTable::Remove(node);
}

void CValueTable::Clear()
{
	if (CBackendException::IsEvalMode())
		return;
	IValueTable::Clear();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(CValueTable, "table", g_valueTableCLSID);

SYSTEM_TYPE_REGISTER(CValueTable::CValueTableColumnCollection, "tableValueColumn", string_to_clsid("VL_TAVC"));
SYSTEM_TYPE_REGISTER(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, "tableValueColumnInfo", string_to_clsid("VL_TVCI"));
SYSTEM_TYPE_REGISTER(CValueTable::CValueTableReturnLine, "tableValueRow", string_to_clsid("VL_TVCR"));