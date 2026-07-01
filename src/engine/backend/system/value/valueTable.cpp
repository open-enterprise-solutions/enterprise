////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value table and key pair 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"
#include "backend/backend_exception.h"

// L5-1 composer — full type to bind the base m_composer to this table's source in the ctor (the RAM
// queryable itself now lives in tableInfo.cpp, generalised onto ibValueModelStorage).
#include "backend/composition/dataComposer.h"   // ibDataDBComposer — m_composer.FromSource(...)

//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////

ibDataViewItem ibValueModelTable::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = m_tableColumnCollection->GetColumnByName(colName);
	if (colInfo != nullptr) {
		for (long row = 0; row < GetRowCount(); row++) {
			const ibDataViewItem& item = GetItem(row);
			ibComposerNode* node = GetViewData<ibComposerNode>(item);
			if (node != nullptr &&
				varValue == node->GetTableValue((ibMetaID)colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return ibDataViewItem(nullptr);
}

ibValueModelTable::ibValueModelTable() : ibValueModelStorage(),
m_tableColumnCollection(new ibValueModelTableColumnCollection(this))
{
	m_members.Bind(this, &ibValueModelTable::FillMembers);
	// (The RAM composer is auto-bound to this model's value-storage in ibValueModelStorage's ctor — no manual bind.)
}

ibValueModelTable::ibValueModelTable(const ibValueModelTable& valueTable) : ibValueModelStorage(),
m_tableColumnCollection(valueTable.m_tableColumnCollection)
{
	m_members.Bind(this, &ibValueModelTable::FillMembers);
	// (RAM composer auto-bound in ibValueModelStorage's ctor.)
}

ibValueModelTable::~ibValueModelTable()
{
}

// NOTE: a table-of-values is a RAM model — it has NO source queryable. The RAM composer (ibDataRamComposer)
// filters/sorts/groups ibValueModelStorage's ibRamValueStorage (the live nodes) IN PLACE. There is no RAM
// query-text / SQL / ibRamTableQueryable door any more.

void ibValueModelTable::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Clone"), wxT("Clone()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Find"), 2, wxT("Find(value : any, column : string)"));
	helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(row : tableRow)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Sort"), 2, wxT("Sort(column : string, ascending = true : boolean)"));

	helper.AppendProp(wxT("Columns"));
}

bool ibValueModelTable::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColumns:
		pvarPropVal = m_tableColumnCollection;
		return true;
	}

	return false;
}

bool ibValueModelTable::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAddRow:
		pvarRetValue = GetRowAt(AppendRow());
		return true;
	case enClone:
		pvarRetValue = Clone();
		return true;
	case enFind:
	{
		const ibDataViewItem& item = FindRowValue(*paParams[0], paParams[1]->GetString());
		if (item.IsOk())
			pvarRetValue = GetRowAt(item);
		return true;
	}
	case enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case enDelete:
	{
		ibValueModelTableReturnLine* retLine = nullptr;
		if (paParams[0]->ConvertToValue(retLine)) {
			ibComposerNode* node = GetViewData<ibComposerNode>(retLine->GetLineItem());
			if (node != nullptr)
				ibValueModelStorage::Remove(node);
		}
		else {
			ibComposerNode* node = GetViewData<ibComposerNode>(GetItem(paParams[0]->GetInteger()));
			if (node != nullptr) ibValueModelStorage::Remove(node);
		}
		return true;
	}
	case enClear:
		Clear();
		return true;
	case enSort: {
		// Sort goes through L5 — NOT an in-memory CompareRow loop (Max: sorting must live on L5, not a loop).
		// Set the composer's ORDER BY (over this table's RAM queryable) by column NAME, then refetch; the
		// composer renders the sorted view. m_sortOrder / RamTableBase::Sort are gone.
		const wxString colName = paParams[0]->GetString();
		if (m_tableColumnCollection->GetColumnByName(colName) == nullptr)
			return false;
		m_composer.ClearSorts();
		m_composer.Sort(colName, lSizeArray > 0 ? paParams[1]->GetBoolean() : true);
		NotifyReset();
		return true;
	}
	}

	return false;
}

#include "backend/appData.h"

bool ibValueModelTable::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	const long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}
	pvarValue = new ibValueModelTableReturnLine(this, GetItem(index));
	return true;
}

//////////////////////////////////////////////////////////////////////
//               ibValueModelTableColumnCollection                        //
//////////////////////////////////////////////////////////////////////


ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnCollection(ibValueModelTable* ownerTable) : ibValueModelColumnCollection(),
m_ownerTable(ownerTable)
{
	m_members.Bind(this, &ibValueModelTableColumnCollection::FillMembers);
}

ibValueModelTable::ibValueModelTableColumnCollection::~ibValueModelTableColumnCollection() {
}

void ibValueModelTable::ibValueModelTableColumnCollection::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("AddColumn"), 4, wxT("Add(name : string, type : typeDescription, caption, width)"));
	helper.AppendProc(wxT("RemoveColumn"), 1, wxT("RemoveColumn(name : string)"));
}

#include "valueType.h"

bool ibValueModelTable::ibValueModelTableColumnCollection::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRemoveColumn:
	{
		wxString columnName = paParams[0]->GetString();
		auto it = std::find_if(m_listColumnInfo.begin(), m_listColumnInfo.end(),
			[columnName](ibValueModelTableColumnInfo* colData)
			{
				return stringUtils::CompareString(columnName, colData->GetColumnName());
			});
		if (it != m_listColumnInfo.end()) {
			RemoveColumn((*it)->GetColumnID());
		}
		return true;
	}
	}
	return false;
}

bool ibValueModelTable::ibValueModelTableColumnCollection::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAddColumn:
	{
		ibValueType* valueType = nullptr;
		if (lSizeArray > 1)
			paParams[1]->ConvertToValue(valueType);
		if (lSizeArray > 3)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? ibTypeDescription(valueType->GetOwnerTypeDescription()) : ibTypeDescription(*paParams[1]->ConvertToType<ibValueTypeDescription>()), paParams[2]->GetString(), paParams[3]->GetInteger());
		else if (lSizeArray > 2)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? ibTypeDescription(valueType->GetOwnerTypeDescription()) : ibTypeDescription(*paParams[1]->ConvertToType<ibValueTypeDescription>()), paParams[2]->GetString(), wxDVC_DEFAULT_WIDTH);
		else if (lSizeArray > 1)
			pvarRetValue = AddColumn(paParams[0]->GetString(), valueType ? ibTypeDescription(valueType->GetOwnerTypeDescription()) : ibTypeDescription(*paParams[1]->ConvertToType<ibValueTypeDescription>()), paParams[0]->GetString(), wxDVC_DEFAULT_WIDTH);
		else
			pvarRetValue = AddColumn(paParams[0]->GetString(), ibTypeDescription(g_valueStringCLSID), paParams[0]->GetString(), wxDVC_DEFAULT_WIDTH);
		return true;
	}
	}

	return false;
}

bool ibValueModelTable::ibValueModelTableColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue) // read-only column collection - no-op (writes are not supported)
{
	return false;
}

bool ibValueModelTable::ibValueModelTableColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) // read a column-info entry by its index
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_listColumnInfo.size() && !appData->DesignerMode())) {
		ibBackendCoreException::Error(_("Index goes beyond array")); 
		return false;
	}
	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = *it;
	return true;
}

//////////////////////////////////////////////////////////////////////
//               ibValueModelTableColumnInfo                              //
//////////////////////////////////////////////////////////////////////


ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::ibValueModelTableColumnInfo() : ibValueModelColumnInfo() {
}

ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::ibValueModelTableColumnInfo(unsigned int colID, const wxString& colName, const ibTypeDescription& typeDescription, const wxString& caption, int width) :
	ibValueModelColumnInfo(), m_columnID(colID), m_columnName(colName), m_columnType(typeDescription), m_columnCaption(caption), m_columnWidth(width) {
}

ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::~ibValueModelTableColumnInfo() {
}

//////////////////////////////////////////////////////////////////////
//               ibValueModelTableReturnLine                              //
//////////////////////////////////////////////////////////////////////


ibValueModelTable::ibValueModelTableReturnLine::ibValueModelTableReturnLine(ibValueModelTable* ownerTable, const ibDataViewItem& line) :
	ibValueModelReturnLine(line), m_ownerTable(ownerTable) {
	m_members.Bind(this, &ibValueModelTableReturnLine::FillMembers);
}

ibValueModelTable::ibValueModelTableReturnLine::~ibValueModelTableReturnLine() {
}

void ibValueModelTable::ibValueModelTableReturnLine::FillMembers(ibMemberTable& helper) const
{
	for (auto& colInfo : m_ownerTable->m_tableColumnCollection->m_listColumnInfo) {
		wxASSERT(colInfo);
		helper.AppendProp(
			colInfo->GetColumnName(),
			colInfo->GetColumnID()
		);
	}
}

bool ibValueModelTable::ibValueModelTableReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (appData->DesignerMode())
		return false;
	return SetValueByMetaID(
		m_members.GetPropData(lPropNum),
		varPropVal
	);
}

bool ibValueModelTable::ibValueModelTableReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;

	return GetValueByMetaID(
		m_members.GetPropData(lPropNum), pvarPropVal
	);
}

//**********************************************************************

long ibValueModelTable::AppendRow(unsigned int before)
{
	ibComposerNode* rowData = new ibComposerNode();
	for (auto& colData : m_tableColumnCollection->m_listColumnInfo) {
		rowData->AppendTableValue(colData->GetColumnID(),
			ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(colData->GetColumnID()))
		);
	}

	// Grouped add: the new row inherits the current group's dimension values (read each grouping dim off the
	// selected row), so it lands INSIDE the group instead of losing the grouping value. Ungrouped → no dims →
	// no-op; a dotted (reference-walk) dim has no single storage column and is skipped. (Grouping is NOT ТЧ-only.)
	if (ibComposerNode* ctx = GetViewData<ibComposerNode>(GetSelection())) {
		for (size_t i = 0; i < GetModelComposer().GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!GetModelComposer().GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			const ibMetaID col = GetColumnIDByName(field);
			if (col != wxNOT_FOUND)
				rowData->AppendTableValue(col, ctx->GetTableValue(col));
		}
	}

	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
}

void ibValueModelTable::EditRow()
{
	ibValueModelStorage::RowValueStartEdit(GetSelection());
}

void ibValueModelTable::CopyRow()
{
	ibDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	// The displayed item is a composer COPY — resolve the REAL storage row (+ index) via the bridge.
	ibComposerNode* node = StorageRowOf(currentItem);
	if (node == nullptr)
		return;
	ibComposerNode* rowData = new ibComposerNode();
	for (auto& colData : m_tableColumnCollection->m_listColumnInfo) {
		rowData->AppendTableValue(
			colData->GetColumnID(), node->GetTableValue(colData->GetColumnID())
		);
	}
	const long currentLine = StorageIndexOf(currentItem);
	if (currentLine != wxNOT_FOUND) {
		ibValueModelStorage::Insert(rowData, currentLine, !ibBackendException::IsEvalMode());
	}
	else {
		ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
	}
}

void ibValueModelTable::DeleteRow()
{
	ibDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	// The displayed item is a composer COPY — Remove needs the REAL storage row (resolved via the bridge).
	ibComposerNode* node = StorageRowOf(currentItem);
	if (node == nullptr)
		return;
	if (!ibBackendException::IsEvalMode())
		ibValueModelStorage::Remove(node);
}

void ibValueModelTable::Clear()
{
	if (ibBackendException::IsEvalMode())
		return;
	ibValueModelStorage::Clear();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueModelTable, "Table", g_valueTableCLSID);

SYSTEM_TYPE_REGISTER(ibValueModelTable::ibValueModelTableColumnCollection, "TableValueColumn", system_to_clsid("VL_TVCLM"));
SYSTEM_TYPE_REGISTER(ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo, "TableValueColumnInfo", system_to_clsid("VL_TVCLI"));
SYSTEM_TYPE_REGISTER(ibValueModelTable::ibValueModelTableReturnLine, "TableValueRow", system_to_clsid("VL_TVROW"));
