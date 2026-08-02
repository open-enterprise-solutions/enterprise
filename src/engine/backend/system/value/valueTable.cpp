////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value table and key pair 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"
#include "backend/backend_exception.h"

// L5-1 composer — full type to bind the base m_composer to this table's source in the ctor (the RAM
// queryable itself now lives in model.cpp, generalised onto ibValueModelStorage).
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
//   ibValueModelTable as a form data source + property object       //
//////////////////////////////////////////////////////////////////////

#include "backend/srcDataObject.h"           // ibSourceExplorer::AppendColumn (already via valueTable.h; explicit)
#include "backend/serialize/dataBuilder.h"   // ibDataNode — column collection round-trip
#include "backend/typeDescription.h"         // ibTypeDescriptionMemory — column type node round-trip
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase : ibMetaData — GetActiveMetaData() base cast
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::CoerceHopType — the reference builds its OWN empty typed twin (type-only step)

const ibMetaData* ibValueModelTable::GetSourceMetaData() const
{
	// A RAM table has no metaobject of its own → expose the ACTIVE config, so reference-typed columns resolve
	// their targets. Mirrors the dynamic list, which gets real config metaData from its queryable; a value-table
	// has none of its own, so it falls to active.
	return ibApplicationData::GetActiveMetaData();
}

// Scalar hop gate — DESIGN-TIME dot-walk (WalkColumns) only. A value-table holds 0..N rows, so there is NO
// single scalar cell to read (unlike a record's one field). The walk does not step by VALUE, it steps by TYPE:
// hand back the pinned branch's empty typed twin. CoerceHopType is the reference's OWN static — the table asks
// the reference to build itself, it does not fabricate reference logic here. We hand it the LIVE column so the
// reference validates the pin against the column's CURRENT type: a column RETYPED in the designer leaves a
// stale pin on a bound path, and CoerceHopType must not resolve the dead old twin. Row count is irrelevant —
// the twin is type-only, via the active config (a RAM table has no metaobject).
bool ibValueModelTable::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	auto* col = m_tableColumnCollection->GetColumnByID(hop.m_id);
	return ibValueReferenceDataObject::CoerceHopType(
		hop, out, col != nullptr ? col->GetColumnType() : ibTypeDescription(), GetSourceMetaData());
}

ibUniqueKey ibValueModelTable::GetGuid() const
{
	// Minted once in the ctor (m_guid, in-class initializer) — stable + unique per RAM-table instance.
	return m_guid;
}

const ibSourceDataObject::ibSourceExplorer* ibValueModelTable::GetSourceExplorer() const
{
	// The columns come from the value-table's OWN column collection. Each column-info IS its own source-
	// column descriptor (ibBackendSourceColumn), so vend it through the descriptor-carrying AppendColumn —
	// the SAME path metadata / dynamic-list columns use. That descriptor (m_col) is what WalkColumns returns
	// as the leaf, so the bound tablebox resolves the header (GetSynonym = Caption) and type LIVE, per-render.
	// Root flagged a TABLE SECTION so the metadata-free IsTableSource() (GetSourceExplorer()->IsTableSection())
	// reports true — a value-table attribute is then draggable AS a tablebox and its columns resolve as table
	// columns, not standalone textboxes.
	m_sourceExplorer.Reset(GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueTableCLSID, /*tableSection*/true);
	for (auto& col : m_tableColumnCollection->m_listColumnInfo) {
		if (col == nullptr)
			continue;
		// The column-info IS the source-column descriptor: col (ibValuePtr) -> the column-info* (operator T*)
		// -> its ibBackendSourceColumn base. id passed explicitly (a RAM source column has no query id of its own).
		const ibBackendSourceColumn* descriptor = col;
		m_sourceExplorer.AppendColumn(descriptor, col->GetColumnID());
	}
	return &m_sourceExplorer;
}

// The value-table exposes no editable ibProperty of its own (its config IS its columns, edited through the
// tablebox) — nothing to recompute when a property changes.
void ibValueModelTable::OnPropertyChanged(ibProperty* /*property*/, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
}

bool ibValueModelTable::WriteProperty(ibDataNode& node) const
{
	// One child node per column; the column-info serializes ITSELF through the unified property mechanism
	// (its own WriteProperty — Name / Caption / Type via each property's GetNodeValue, + id / width). No
	// hand-rolled field writing / ibTypeDescriptionMemory here — the same path ibFormAttribute uses.
	for (auto& col : m_tableColumnCollection->m_listColumnInfo) {
		if (col == nullptr)
			continue;
		ibDataNode& colNode = node.AddChild(g_valueTableCLSID, col->GetColumnID());
		col->WriteProperty(colNode);
	}
	return true;
}

bool ibValueModelTable::ReadProperty(const ibDataNode& node)
{
	// Rebuild the column collection from the serialized child nodes. Clear first so a re-read stays
	// idempotent; add an empty column, then let it read ITSELF back through its property serialization (no
	// rows exist yet, so AddColumn's per-row cell-fill is a no-op).
	m_tableColumnCollection->m_listColumnInfo.clear();
	for (const ibDataNode& colNode : node.Children()) {
		if (m_tableColumnCollection->AddColumn(wxEmptyString, ibTypeDescription(), wxEmptyString, wxDVC_DEFAULT_WIDTH) != nullptr)
			m_tableColumnCollection->m_listColumnInfo.back()->ReadProperty(colNode);
	}
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
	// `index` is unsigned, so `index < 0` was dead code, and && binds tighter than ||
	// — the condition already meant "out of range AND not in the designer". Spelled out;
	// the designer-mode exemption is preserved, not introduced (see docs/portability.md).
	if (index >= m_listColumnInfo.size() && !appData->DesignerMode()) {
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
	ibValueModelColumnInfo(), m_columnID(colID), m_columnWidth(width) {
	// The property variants ARE the storage (no parallel members) — seed them from the ctor args.
	m_propertyName->SetValue(colName);
	m_propertyCaption->SetValue(caption);
	m_propertyType->SetValue(typeDescription);
}

ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::~ibValueModelTableColumnInfo() {
}

const ibMetaData* ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::GetMetaData() const
{
	// The column has no metaobject of its own — expose the active configuration so a reference-typed
	// column resolves its targets (mirror ibValueModelTable::GetSourceMetaData / ibFormAttribute).
	return ibApplicationData::GetActiveMetaData();
}

void ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::OnPropertyChanged(ibProperty* /*property*/, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	// Nothing to sync: the property variants ARE the storage, so an inspector edit is already the live value
	// every reader (GetColumnType / GetTypeDesc / GetSynonym / the source explorer) returns. A TYPE change is
	// NOT swept over the rows here — the value-table coerces each cell to the column's CURRENT type LAZILY on
	// read (ibValueModelTable::GetValueByMetaID / GetValueByRow), so a stale-typed cell reads back retyped with
	// no eager loop (there are usually no rows at design time anyway).
}

bool ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::WriteProperty(ibDataNode& node) const
{
	// Serialize through the UNIFIED property mechanism (each property writes itself via GetNodeValue —
	// the same path ibFormAttribute uses, so the Type round-trips through ibPropertyType, not a hand-rolled
	// ibTypeDescriptionMemory), plus the non-property id / width.
	node.SetValue<s32>(wxT("id"), (s32)m_columnID);
	node.SetValue<s32>(wxT("width"), m_columnWidth);
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyType->GetName(), m_propertyType->GetNodeValue());
	return ibPropertyObject::WriteProperty(node);
}

bool ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo::ReadProperty(const ibDataNode& node)
{
	m_columnID = (unsigned int)node.GetValue<s32>(wxT("id"));
	m_columnWidth = node.GetValue<s32>(wxT("width"));
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->ReadNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyType->ReadNodeValue(node.GetProperty(m_propertyType->GetName()));
	return ibPropertyObject::ReadProperty(node);
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

long ibValueModelTable::AppendRow(unsigned int before, const ibDataViewItem& contextRow)
{
	ibComposerNode* rowData = new ibComposerNode();
	for (auto& colData : m_tableColumnCollection->m_listColumnInfo) {
		rowData->AppendTableValue(colData->GetColumnID(),
			ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(colData->GetColumnID()))
		);
	}

	// Grouped add: the new row inherits the current group's dimension values (read each grouping dim off the
	// FRONT-passed context row — the selected row), so it lands INSIDE the group instead of losing the grouping
	// value. No context / ungrouped → no dims → no-op; a dotted (reference-walk) dim is skipped. (NOT tabular-section-only.)
	if (ibComposerNode* ctx = GetViewData<ibComposerNode>(contextRow)) {
		for (size_t i = 0; i < GetModelComposer().GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!GetModelComposer().GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			const ibMetaID col = GetColumnIDByName(field);
			if (col != wxNOT_FOUND)
				rowData->AppendTableValue(col, ctx->GetTableValue(col));
		}
	}

	// Insert AFTER the active row when AddValue passes a position (before > 0); before == 0 → append at the bottom
	// (the script Add() path). Mirrors the tabular section so both editable tables place a new row the same way.
	if (before > 0)
		return ibValueModelStorage::Insert(rowData, before, !ibBackendException::IsEvalMode());
	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
}

void ibValueModelTable::EditRow(const ibDataViewItem& row)
{
	// Inline editing is opened by the TableBox on the control (front, OnItemActivated → EditItem) — the model no
	// longer tells the control to start editing. (Was RowValueStartEdit → notifier StartEditing, now gone.)
	(void)row;
}

void ibValueModelTable::CopyRow(const ibDataViewItem& row)
{
	ibDataViewItem currentItem = row;
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
	// Copy goes AFTER the source row so the original keeps its position; the new row lands at currentLine + 1 and
	// the ItemInserted handler moves focus onto it via Select. (Mirrors the tabular section.)
	const long currentLine = StorageIndexOf(currentItem);
	if (currentLine != wxNOT_FOUND) {
		ibValueModelStorage::Insert(rowData, currentLine + 1, !ibBackendException::IsEvalMode());
	}
	else {
		ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
	}
}

void ibValueModelTable::DeleteRow(const ibDataViewItem& row)
{
	ibDataViewItem currentItem = row;
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
