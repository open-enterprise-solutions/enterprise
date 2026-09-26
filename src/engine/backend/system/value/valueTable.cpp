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

		// ⭐⭐ THE INDEX ANSWERS FIRST WHEN THE COLUMN ASKED FOR ONE, and this line is the WHOLE of
		// what this class has to say about indexing: the machinery is maintained one level up, by
		// the model that holds the rows (ibValueModelStorage), because a tabular section and a
		// record set hold theirs the same way and are searched the same way.
		//
		// What is decided HERE is only whether to ask — and the decision is the COLUMN'S
		// (`Indexing`), an author's statement about THIS column rather than a policy the table
		// imposes. The scan below stays as the answer for every column that declared none.
		if (colInfo->IsColumnIndexed())
			return GetRowByValue(colInfo->GetColumnID(), varValue);

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

// ⭐ A COPY IS THE SAME COLUMNS AND THE SAME ROWS, AND NOTHING SHARED. It was `new ibValueModelTable(*this)`,
// and that copy constructor built no rows and took the original's column collection itself: `t.Clone()`
// came back EMPTY, and a column added to the copy was added to the original (read 2026-09-21; no test
// had asked). The columns are declared again - name, type, caption, width, index - and each row's
// cells are carried across by the column they stand in; the values themselves are copied as values, so a
// reference in a cell is the same reference in both.
ibValuePtr<ibValueModelTable> ibValueModelTable::Clone() const
{
	ibValuePtr<ibValueModelTable> copy(new ibValueModelTable());

	std::vector<std::pair<ibMetaID, ibMetaID>> columns;   // a column here -> the same column there
	for (unsigned int i = 0; i < m_tableColumnCollection->GetColumnCount(); ++i) {
		const ibValueModelColumnCollection::ibValueModelColumnInfo* const from = m_tableColumnCollection->GetColumnInfo(i);
		if (from == nullptr)
			continue;
		ibValueModelColumnCollection::ibValueModelColumnInfo* const made = copy->m_tableColumnCollection->AddColumn(
			from->GetColumnName(), from->GetColumnType(), from->GetColumnCaption(), from->GetColumnWidth());
		if (made == nullptr)
			continue;
		made->SetColumnIndexed(from->IsColumnIndexed());
		columns.emplace_back(static_cast<ibMetaID>(from->GetColumnID()), static_cast<ibMetaID>(made->GetColumnID()));
	}

	for (long row = 0; row < GetRowCount(); ++row) {
		ibComposerNode* const from = GetViewData<ibComposerNode>(GetItem(row));
		if (from == nullptr)
			continue;
		const long at = copy->AppendRow();
		ibComposerNode* const to = copy->GetViewData<ibComposerNode>(copy->GetItem(at));
		if (to == nullptr)
			continue;
		for (const std::pair<ibMetaID, ibMetaID>& column : columns)
			to->SetValue(column.second, from->GetTableValue(column.first));
	}
	return copy;
}

ibValueModelTable::~ibValueModelTable()
{
	// 🛑 THE COLUMNS OUTLIVE THE TABLE, so the table says goodbye rather than leaving a pointer at
	// itself. `t.Columns` hands the collection out as a value: whoever holds it keeps it alive, and
	// its back-pointer here was BARE — after the table went, AddColumn walked the rows of freed
	// memory (measured 2026-09-08 from a crash dump, on `c = t.Columns.AddColumn("Code"); c.`).
	//
	// ⚠ The row's fix — hold the owner — cannot be used here: the table OWNS the collection, so an
	// owning back-pointer is a cycle and the table would never be freed. Detaching is the honest
	// shape for that direction, and it is the one the row base already uses for a detached line: an
	// object that has outlived its owner answers "no owner", it does not dereference one.
	//
	// 🛑 …AND SO DO ITS COLUMNS. AddColumn gives each column this table as its attach owner
	// (SetAttachOwner, not AttachPropertyObject — see there), so the property object's own teardown
	// does not know them, and a column that died after the table asked the freed table to forget it
	// (crash dump 2026-09-27: completion held `Total.Columns` after `Total` had gone).
	if (m_tableColumnCollection != nullptr) {
		for (const auto& colInfo : m_tableColumnCollection->m_listColumnInfo)
			colInfo->SetAttachOwner(nullptr);
		m_tableColumnCollection->DetachOwnerTable();
	}
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
	helper.AppendFunc(wxT("UnloadColumn"), 1, wxT("UnloadColumn(column : string) : array"));
	helper.AppendFunc(wxT("Total"), 1, wxT("Total(column : string) : number"));
	helper.AppendFunc(wxT("FindRows"), 1, wxT("FindRows(filter : structure) : array"));

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

	// ⭐⭐ THE BARE VALUES OF ONE COLUMN, ASKED FOR EXPLICITLY. This is the other half of "a result is
	// always a table": a caller that wants plain values says so, instead of the pipeline guessing
	// from context whether to hand back a row, a reference or a scalar. In row order, so it lines up
	// with the table it came from.
	case enUnloadColumn: {
		ibValueArray* const values = new ibValueArray();
		pvarRetValue = values;   // the wrapper owns it

		const ibValueModelColumnCollection::ibValueModelColumnInfo* const colInfo =
			m_tableColumnCollection != nullptr
				? m_tableColumnCollection->GetColumnByName(paParams[0]->GetString()) : nullptr;
		if (colInfo == nullptr)
			return true;   // no such column: an empty array, not an error — the same answer Find gives

		for (long row = 0; row < GetRowCount(); row++) {
			ibComposerNode* const node = GetViewData<ibComposerNode>(GetItem(row));
			if (node != nullptr)
				values->Add(node->GetTableValue((ibMetaID)colInfo->GetColumnID()));
		}
		return true;
	}

	case enSort: {
		// ONE MEANING OF "SORT" for this table: the script's Sort() re-seats the rows, exactly as the two
		// order commands do. A script that sorts a table and then walks it must walk it sorted.
		//
		// 🛑 ONE COLUMN. An order over several keys is a query's to say - `from r in t orderby r.A, r.B` - and not
		// a second, smaller query language parsed out of a string argument here.
		ibDataViewColumnItem column;
		column.m_name = paParams[0]->GetString();
		if (m_tableColumnCollection->GetColumnByName(column.m_name) == nullptr)
			ibBackendCoreException::Error(_("Table column '%s' not found"), column.m_name);
		// (⚠ It read paParams[1] whenever there was ANY argument - `Sort("A")` looked one past what was given.)
		SortValue(column, lSizeArray > 1 ? paParams[1]->GetBoolean() : true);
		return true;
	}

	// ⭐⭐ THE TOTAL OF A COLUMN and THE ROWS THAT MATCH - see the two methods below.
	case enTotal:
		pvarRetValue = TotalOf(paParams[0]->GetString());
		return true;

	case enFindRows: {
		// ConvertToValue, not ConvertToType: it CHECKS what the argument is (a string handed in by mistake is
		// answered with the message below), where the raw cast has no way to say "not one of these".
		ibValueContainer* filter = nullptr;
		if (paParams[0] == nullptr || !paParams[0]->ConvertToValue(filter))
			ibBackendCoreException::Error(_("FindRows expects a Structure of column = value"));
		pvarRetValue = FindRows(*filter);
		return true;
	}
	}

	return false;
}

#include "backend/appData.h"

bool ibValueModelTable::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	const long index = varKeyValue.GetUInteger();
	// ⚠ THE DESIGNER EXEMPTION IS ASKED OF SOMETHING THAT NEED NOT BE THERE. `appData` is null in a
	// headless host — a test binary, a tool linking the backend alone — and this dereferenced it
	// with no guard, so an out-of-range index answered with an ACCESS VIOLATION instead of the
	// message one line below (measured 2026-09-09, oes_tests). No appData means no designer, which
	// is also the stricter reading: the bounds error is raised rather than waved through.
	if (index >= GetRowCount() && (appData == nullptr || !appData->DesignerMode())) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}
	pvarValue = new ibValueModelTableReturnLine(this, GetItem(index));
	return true;
}

//////////////////////////////////////////////////////////////////////
//   ibValueModelTable as a form data source + property object       //
//////////////////////////////////////////////////////////////////////

#include "backend/serialize/dataBuilder.h"   // ibDataNode — column collection round-trip
#include "backend/typeDescription.h"         // ibTypeDescriptionMemory — column type node round-trip
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase : ibMetaData — GetActiveMetaData() base cast

const ibMetaData* ibValueModelTable::GetSourceMetaData() const
{
	// A RAM table has no metaobject of its own → expose the ACTIVE config, so reference-typed columns resolve
	// their targets. Mirrors the dynamic list, which gets real config metaData from its queryable; a value-table
	// has none of its own, so it falls to active.
	return ibApplicationData::GetActiveMetaData();
}

const ibUniqueKey& ibValueModelTable::GetGuid() const
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
		// ⚠ THE ID IS SAID AS AN ID. `GetColumnID` answers unsigned, which converts equally well to
		// `ibMetaID` and to `bool` — and since a deleted `(column, bool)` overload now guards against
		// a FLAG being mistaken for an id (srcDataObject.h), an unsigned argument is ambiguous between
		// the two. The cast is not ceremony: this parameter is the id, and saying so is what makes it
		// impossible for the next reader to think it is the `enabled` flag.
		m_sourceExplorer.AppendColumn(descriptor, static_cast<ibMetaID>(col->GetColumnID()));
	}
	return &m_sourceExplorer;
}

// The value-table exposes no editable ibProperty of its own (its config IS its columns, edited through the
// tablebox) — nothing to recompute when a property changes.
void ibValueModelTable::OnPropertyChanged(ibProperty* /*property*/, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
}

// A column changed — edited in the inspector, the one place a column is renamed or given another type. The rows are
// named after the columns and a new row is made of them, so both are made again at the next question; the news goes
// on up to the holder.
void ibValueModelTable::OnChildChanged()
{
	OnColumnsChanged();
	ibPropertyObject::OnChildChanged();
}

void ibValueModelTable::DescribeNewRow(ibNewRowColumns& columns) const
{
	for (auto& colData : m_tableColumnCollection->m_listColumnInfo) {
		const ibTypeDescription type = colData->GetTypeDesc();   // the column's type as it stands now — a change forgets the row
		columns.push_back({ colData->GetColumnID(), [type]() { return ibValueTypeDescription::AdjustValue(type); } });
	}
}

bool ibValueModelTable::WriteProperty(ibDataNode& node) const
{
	// ⭐⭐ THE TABLE'S OWN IDENTITY, WRITTEN DOWN. It is minted in the ctor, so an unserialised table gets
	// a fresh one every run — and anything that ADDRESSES the table by it then addresses a different
	// table each time. Saved settings are exactly that: the shelf is keyed by what is shown, so a reader
	// who kept an arrangement for this table found the shelf empty on the next open, the entry still
	// sitting under last run's guid (Max, 2026-08-29: *"you write this guid there, and when it is read it
	// is restored from there, and it stands stable"*).
	//
	// It rides with the COLUMNS because it is a fact about the same thing they are — what the designer
	// declared. A table created in a SCRIPT is never written, keeps the minted one, and is unaddressable
	// by design: nobody declared it, so nobody can have kept anything for it.
	node.SetProp(wxT("Guid"), m_guid.GetGuid());

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
	// …AND TAKEN BACK, so the table is the same table it was last time. Kept ONLY when something was
	// written: a configuration saved before this existed carries no guid, and overwriting the minted one
	// with a null would give every such table one shared address — which is worse than a fresh one.
	const ibGuid stored = node.GetProp<ibGuid>(wxT("Guid"));
	if (stored.isValid())
		m_guid = stored;

	// Rebuild the column collection from the serialized child nodes. Clear first so a re-read stays
	// idempotent; add an empty column, then let it read ITSELF back through its property serialization (no
	// rows exist yet, so AddColumn's per-row cell-fill is a no-op).
	m_tableColumnCollection->m_listColumnInfo.clear();
	for (const ibDataNode& colNode : node.Children()) {
		if (m_tableColumnCollection->AddColumn(wxEmptyString, ibTypeDescription(), wxEmptyString, wxDVC_DEFAULT_WIDTH) != nullptr)
			m_tableColumnCollection->m_listColumnInfo.back()->ReadProperty(colNode);
	}
	// The columns were emptied and each read its name and type AFTER it was added — so the rows' names and the
	// empty row are forgotten once, here, over what the table now is.
	OnColumnsChanged();
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
	// The verb's own name, and the DEFAULT said out loud: without a type the column holds text, and a
	// filter over it compares as text. Both halves were missing — the text named `Add`, and nothing
	// said what an omitted type means.
	helper.AppendFunc(wxT("AddColumn"), 4, wxT("AddColumn(name : string, type : typeDescription = string, caption, width)"));
	helper.AppendProc(wxT("RemoveColumn"), 1, wxT("RemoveColumn(name : string)"));
}

#include "valueType.h"

bool ibValueModelTable::ibValueModelTableColumnCollection::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRemoveColumn:
	{
		const wxString columnName = paParams[0]->GetString();
		if (ibValueModelColumnInfo* colInfo = GetColumnByName(columnName))
			RemoveColumn(colInfo->GetColumnID());
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
			// ⚠ NO TYPE GIVEN MEANS A STRING COLUMN, and that is a DECISION with consequences a
			// caller has to know about: whatever is written into the cell, the column keeps saying
			// it holds text, and everything that compares by the COLUMN's type then compares as
			// text. `AddColumn("N")` + `row.N = 5` + `Where(x.N > 100)` keeps the row, because "5"
			// sorts after "100" (measured 2026-09-04 — it reads as a broken filter and is not one).
			//
			// Left as it is on purpose: changing the default would change what every existing table
			// holds. Say it in the signature instead, so the caller passes a type when they mean a
			// number — `AddColumn("N", New TypeDescription("Number"))` compares as a number.
			//
			// ⚠ AND A STRING OF ANY LENGTH (Unqualified). The bare string type carried the designer's
			// default of ten characters, so a cell kept the first ten of whatever was written into it
			// (measured 2026-09-21).
			pvarRetValue = AddColumn(paParams[0]->GetString(),
				ibTypeDescription(g_valueStringCLSID, ibValueTypeDescription::Unqualified()),
				paParams[0]->GetString(), wxDVC_DEFAULT_WIDTH);
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
	if (index >= m_listColumnInfo.size() && (appData == nullptr || !appData->DesignerMode())) {
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
	m_propertyName->SetNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->SetNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyType->SetNodeValue(node.GetProperty(m_propertyType->GetName()));
	return ibPropertyObject::ReadProperty(node);
}

//////////////////////////////////////////////////////////////////////
//               ibValueModelTableReturnLine                              //
//////////////////////////////////////////////////////////////////////


ibValueModelTable::ibValueModelTableReturnLine::ibValueModelTableReturnLine(ibValueModelTable* ownerTable, const ibDataViewItem& line) :
	ibValueModelReturnLine(line), m_ownerTable(ownerTable) {
	HoldOwnerModel(ownerTable);   // the row speaks through the table; see the base
}

ibValueModelTable::ibValueModelTableReturnLine::~ibValueModelTableReturnLine() {
}

bool ibValueModelTable::ibValueModelTableReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	// See GetPropVal below for why the null is asked about at all.
	if (appData != nullptr && appData->DesignerMode())
		return false;
	return SetValueByMetaID(
		m_ownerTable->m_methodHelperReturnLine.GetPropData(lPropNum),
		varPropVal
	);
}

bool ibValueModelTable::ibValueModelTableReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// 🛑 `appData` NEED NOT EXIST. It is the running application, and the backend is also linked by
	// hosts that have none — the test binary is one. Reading a column off a query's answer row went
	// straight through here and took the process down with an ACCESS VIOLATION (measured 2026-09-09,
	// oes_tests: `q[0].V` on a LINQ result). No application means no designer, so the read proceeds,
	// which is what a headless host wants; the designer's refusal is unchanged where it applies.
	if (appData != nullptr && appData->DesignerMode())
		return false;

	return GetValueByMetaID(
		m_ownerTable->m_methodHelperReturnLine.GetPropData(lPropNum), pvarPropVal
	);
}

//**********************************************************************

namespace {
// A filter line DETERMINES a value only when it SAYS one: an equality, switched on, on a plain column (a
// dotted path walks into a reference — there is no cell of this table to write it into), against a literal
// rather than another field. `>` / `LIKE` / `IN` narrow without deciding, and an OR-group decides nothing at
// all — so neither contributes, and an OR is not walked into.
void ibCollectFilterDefaults(const std::vector<ibFilterNodeDescription>& nodes,
	std::vector<std::pair<wxString, ibValue>>& out)
{
	for (const ibFilterNodeDescription& node : nodes) {
		if (!node.m_use)
			continue;
		if (node.m_kind == ibFilterNodeKind_Group) {
			if (node.m_groupKind == ibFilterGroupKind_And)
				ibCollectFilterDefaults(node.m_children, out);
			continue;
		}
		if (node.m_comparison != ibComparisonKind_Equal) continue;
		if (!node.m_left.IsField() || node.m_right.IsField()) continue;
		if (node.m_left.m_path.Find(wxT('.')) != wxNOT_FOUND) continue;
		out.emplace_back(node.m_left.m_path, node.m_right.m_value);
	}
}
}   // namespace

long ibValueModelTable::AppendRow(unsigned int before, const ibDataViewItem& contextRow)
{
	// A copy of the table's own empty row (ibValueModelStorage::NewRow) — made once, from the columns as they stand.
	ibComposerNode* rowData = NewRow();

	// Grouped add: the new row inherits the dimension values of the group the user is INSIDE — the drilled-into
	// folder the front passes as the context — so it lands in that group instead of losing the value. At the
	// ROOT it inherits nothing and therefore forms an empty group of its own, which a person can step into and
	// fill; filling it re-folds the view on the spot. Ungrouped → no dims → no-op; a dotted dim is skipped.
	if (ibComposerNode* ctx = GetViewData<ibComposerNode>(contextRow)) {
		for (size_t i = 0; i < GetModelComposer().GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!GetModelComposer().GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			const ibMetaID col = GetColumnIDByName(field);
			if (col != wxNOT_FOUND)
				rowData->AppendTableValue(col, ctx->GetTableValue(col));
		}
	}

	// ⭐⭐ …AND WHAT THE FILTER IN FORCE HAS ALREADY DECIDED. Not a convenience: the new row is empty, the filter
	// does not pass it, and the RAM composer drops it from the order in the same breath — the row a person just
	// added is gone before they see it. Filled HERE, before the notify, so the first order computed after the
	// insert already contains the row. Adjusted to the COLUMN's type, like every other cell of this table.
	std::vector<std::pair<wxString, ibValue>> defaults;
	ibCollectFilterDefaults(GetModelComposer().GetCurrentFilterDesc().m_nodes, defaults);
	for (const std::pair<wxString, ibValue>& one : defaults) {
		const ibMetaID col = GetColumnIDByName(one.first);
		if (col != wxNOT_FOUND)
			rowData->AppendTableValue(col,
				ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnTypeDesc(col), one.second));
	}

	// Insert AFTER the active row when AddValue passes a position (before > 0); before == 0 → append at the bottom
	// (the script Add() path). Mirrors the tabular section so both editable tables place a new row the same way.
	if (before > 0)
		return ibValueModelStorage::Insert(rowData, before, !ibBackendException::IsEvalMode());
	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
}

long ibValueModelTable::AppendRow(const std::vector<std::pair<ibMetaID, ibValue>>& values)
{
	// The table's empty row first, as for a row a script adds: a column the values do not name holds its type's empty
	// value rather than no value at all.
	ibComposerNode* rowData = NewRow();
	for (const std::pair<ibMetaID, ibValue>& one : values)
		rowData->AppendTableValue(one.first,
			ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnTypeDesc(one.first), one.second));
	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
}

void ibValueModelTable::SortValue(const ibDataViewColumnItem& column, bool ascending)
{
	if (!column.IsOk() || ibBackendException::IsEvalMode())
		return;

	// ⭐⭐ A SORT CHANGES THE DATA — the rows are re-seated, and that is the order the table then IS (Max,
	// 2026-08-29: moving and sorting change the rows physically; a filter, a grouping or a setting is a layer
	// ABOVE them and dies with the window). The composer's own sort is dropped again at the end: the rows now
	// SIT in this order, and a read-order left on top would go on answering over them.
	//
	// L5-2 says WHAT the order is — nothing here compares — and the rows are then placed into it with the
	// storage's own move. Sorting lives on L5, not in a loop.
	ibDataRamComposer& composer = GetModelComposer();
	composer.ClearSorts();
	composer.Sort(column.m_name, ascending);
	Storage().SetColumns(GetColumnCollection());
	const std::vector<long> order = composer.ComputeOrder();
	composer.ClearSorts();

	// The rows in their new sequence, taken BEFORE anything moves — every move shifts the indices under it.
	std::vector<ibComposerNode*> seated;
	seated.reserve(order.size());
	for (const long index : order)
		if (ibComposerNode* node = Storage().GetNode(index))
			seated.push_back(node);

	// ⚠ Rows a FILTER hides are not in the order, so they come to rest AFTER the sorted ones, keeping their
	// own relative order among themselves. Sorting what is shown cannot avoid saying something about what is not.
	for (size_t position = 0; position < seated.size(); ++position) {
		const long current = Storage().IndexOf(seated[position]);
		if (current >= 0 && current != static_cast<long>(position))
			Storage().MoveValue(this, seated[position],
				static_cast<int>(static_cast<long>(position) - current), /*notify*/ false);
	}

	NotifyReset();
}

ibValue ibValueModelTable::TotalOf(const wxString& column) const
{
	const ibValueModelColumnCollection::ibValueModelColumnInfo* const colInfo =
		m_tableColumnCollection != nullptr ? m_tableColumnCollection->GetColumnByName(column) : nullptr;
	if (colInfo == nullptr)
		ibBackendCoreException::Error(_("Table column '%s' not found"), column);

	const ibMetaID id = static_cast<ibMetaID>(colInfo->GetColumnID());
	ibNumber total(0);
	for (long row = 0; row < GetRowCount(); ++row) {
		ibComposerNode* const node = GetViewData<ibComposerNode>(GetItem(row));
		if (node == nullptr)
			continue;
		const ibValue cell = node->GetTableValue(id);
		if (cell.GetType() == ibValueTypes::TYPE_NUMBER) {
			total += cell.GetNumber();
			continue;
		}
		if (cell.IsEmpty())
			continue;   // nothing there adds nothing
		ibBackendCoreException::Error(_("Table column '%s' holds a value that is not a number (row %d); a column added without a type holds text - declare it as a Number"),
			column, static_cast<int>(row + 1));
	}
	return ibValue(total);
}

ibValue ibValueModelTable::FindRows(const ibValueContainer& filter)
{
	// The filter's columns are resolved FIRST: a name that is not a column is a mistake worth seeing, and
	// "no rows" would be the wrong way to say it.
	struct Term { ibMetaID m_id; ibValue m_value; bool m_indexed; };
	std::vector<Term> terms;
	for (const std::pair<ibValue, ibValue>& entry : filter.Entries()) {
		const wxString name = entry.first.GetString();
		const ibValueModelColumnCollection::ibValueModelColumnInfo* const colInfo =
			m_tableColumnCollection != nullptr ? m_tableColumnCollection->GetColumnByName(name) : nullptr;
		if (colInfo == nullptr)
			ibBackendCoreException::Error(_("Table column '%s' not found"), name);
		terms.push_back({ static_cast<ibMetaID>(colInfo->GetColumnID()), entry.second, colInfo->IsColumnIndexed() });
	}

	// Born held (development.md, "A new value is born owned").
	ibValuePtr<ibValueArray> found(new ibValueArray());

	// A row is taken when EVERY term matches it.
	const auto take = [&](const ibDataViewItem& item) {
		ibComposerNode* const node = GetViewData<ibComposerNode>(item);
		if (node == nullptr)
			return;
		for (const Term& term : terms)
			if (!(term.m_value == node->GetTableValue(term.m_id)))
				return;
		ibValue line;
		line = GetRowAt(item);
		found->Add(line);
	};

	// ⭐ THE INDEX ANSWERS FIRST WHEN A TERM'S COLUMN ASKED FOR ONE, as it does for Find (FindRowValue): that
	// term's rows - already in row order - are the only candidates, and the other terms only thin them. With
	// no indexed column in the filter every row is a candidate.
	const Term* byIndex = nullptr;
	for (const Term& term : terms) {
		if (term.m_indexed) {
			byIndex = &term;
			break;
		}
	}

	if (byIndex != nullptr) {
		const std::vector<ibDataViewItem>* const rows =
			GetRowsByValue(static_cast<unsigned int>(byIndex->m_id), byIndex->m_value);
		if (rows != nullptr)
			for (const ibDataViewItem& item : *rows)
				take(item);
	}
	else {
		for (long row = 0; row < GetRowCount(); ++row)
			take(GetItem(row));
	}
	return found;
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
