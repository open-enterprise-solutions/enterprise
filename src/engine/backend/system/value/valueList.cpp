#include "valueList.h"

#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"
#include "backend/appData.h"                 // ibApplicationInstance::GetActiveMetaData — a picked reference resolves there
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase : ibMetaData — the active one, as a base
#include "core/picturePredefined.h"          // g_pic*CLSID — the table's command icons

//////////////////////////////////////////////////////////////////////
//               the kind's columns                                   //
//////////////////////////////////////////////////////////////////////

ibValueValueList::ibValueValueListColumnCollection::ibValueValueListColumnCollection() : ibValueModelColumnCollection()
{
	// The value takes anything; the presentation is text and the check a mark. The picture is kept as it was given.
	m_columns.emplace_back(new ibValueValueListColumnInfo(eValue, ibT("Value"), _("Value"), ibTypeDescription()));
	m_columns.emplace_back(new ibValueValueListColumnInfo(ePresentation, ibT("Presentation"), _("Presentation"),
		ibTypeDescription(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING))));
	m_columns.emplace_back(new ibValueValueListColumnInfo(eCheck, ibT("Check"), _("Check"),
		ibTypeDescription(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN))));
	m_columns.emplace_back(new ibValueValueListColumnInfo(ePicture, ibT("Picture"), _("Picture"), ibTypeDescription()));
}

//////////////////////////////////////////////////////////////////////
//               the item                                             //
//////////////////////////////////////////////////////////////////////

ibValueValueList::ibValueValueListItem::ibValueValueListItem(ibValueValueList* ownerList, const ibDataViewItem& line) :
	ibValueModelReturnLine(line), m_ownerList(ownerList)
{
	HoldOwnerModel(ownerList);   // the item speaks through the list; see the base
}

bool ibValueValueList::ibValueValueListItem::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return SetValueByMetaID(m_ownerList->m_methodHelperReturnLine.GetPropData(lPropNum), varPropVal);
}

bool ibValueValueList::ibValueValueListItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetValueByMetaID(m_ownerList->m_methodHelperReturnLine.GetPropData(lPropNum), pvarPropVal);
}

//////////////////////////////////////////////////////////////////////
//               the list                                             //
//////////////////////////////////////////////////////////////////////

ibValueValueList::ibValueValueList() : ibValueModelStorage(),
	m_columns(new ibValueValueListColumnCollection())
{
	m_members.Bind(this, &ibValueValueList::FillMembers);
	Storage().SetColumns(m_columns);
}

ibValueValueList::~ibValueValueList()
{
}

void ibValueValueList::FillMembers(ibMemberTable& helper) const
{
	helper.AppendConstructor(0, ibT("ValueList()"));
	// The picture is kept with the item; a call that leaves it out gives the item none.
	helper.AppendFunc(ibT("Add"), 4, ibT("Add(value, presentation, check, picture)"));
	helper.AppendFunc(ibT("Insert"), 5, ibT("Insert(index, value, presentation, check, picture)"));
	helper.AppendFunc(ibT("Count"), ibT("Count()"));
	helper.AppendFunc(ibT("Get"), 1, ibT("Get(index)"));
	helper.AppendFunc(ibT("Delete"), 1, ibT("Delete(item)"));
	helper.AppendFunc(ibT("Clear"), ibT("Clear()"));
	helper.AppendFunc(ibT("FindByValue"), 1, ibT("FindByValue(value)"));
	helper.AppendFunc(ibT("UnloadValues"), ibT("UnloadValues()"));
	helper.AppendFunc(ibT("LoadValues"), 1, ibT("LoadValues(array)"));
	helper.AppendFunc(ibT("Copy"), ibT("Copy()"));
	helper.AppendFunc(ibT("SortByValue"), 1, ibT("SortByValue(descending = false)"));
	helper.AppendFunc(ibT("SortByPresentation"), 1, ibT("SortByPresentation(descending = false)"));
	helper.AppendFunc(ibT("FillChecks"), 1, ibT("FillChecks(check)"));
	helper.AppendFunc(ibT("Move"), 2, ibT("Move(item, offset)"));
	helper.AppendFunc(ibT("Index"), 1, ibT("Index(item) : number"));
}

// A cell of the kind's columns takes the kind's type: the presentation is text and the check a mark, whatever is
// written into them. The value and the picture are kept as given.
static ibValue ibValueListCell(const ibMetaID& id, const ibValue& value)
{
	switch (id) {
	case ibValueValueList::ePresentation: return ibValue(value.GetString());
	case ibValueValueList::eCheck: return ibValue(value.GetBoolean());
	}
	return value;
}

bool ibValueValueList::SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(item);
	if (node == nullptr)
		return false;
	return node->SetValue(id, ibValueListCell(id, varMetaVal), true);
}

bool ibValueValueList::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(item);
	if (node == nullptr)
		return false;
	node->GetValue(id, pvarMetaVal);
	return true;
}

void ibValueValueList::GetValueByRow(ibVariant& variant, const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	// An item with no presentation of its own shows its value.
	if (col == ePresentation && node->GetTableValue(ePresentation).GetString().IsEmpty()) {
		variant = ibValue(node->GetTableValue(eValue).GetString());
		return;
	}
	node->GetValue(col, variant);
}

bool ibValueValueList::SetValueByRow(const ibVariant& variant, const ibDataViewItem& row, unsigned int col)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	// A value typed into the cell is found as the table finds one: by its text, among what the cell can hold.
	if (col == eValue || col == ePicture) {
		const ibString& text = variant.GetString();
		ibValue cell; node->GetValue(col, cell);
		std::vector<ibValue> found;
		if (!text.IsEmpty() && cell.FindValue(text, found))
			return node->SetValue(col, found.at(0));
		return node->SetValue(col, ibValue(variant));
	}
	return node->SetValue(col, ibValueListCell(col, ibValue(variant)));
}

void ibValueValueList::DescribeNewRow(ibNewRowColumns& columns) const
{
	columns.push_back({ eValue, []() { return ibValue(); } });
	columns.push_back({ ePresentation, []() { return ibValue(ibString()); } });
	columns.push_back({ eCheck, []() { return ibValue(false); } });
	columns.push_back({ ePicture, []() { return ibValue(); } });
}

void ibValueValueList::GetCommandCollection(const ibFormID& /*formType*/, std::vector<ibCommandItem>& commands) const
{
	commands.emplace_back(eAddValue, ibT("Add"), _("Add"), g_picAddCLSID, true);
	commands.emplace_back(eDeleteValue, ibT("Delete"), _("Delete"), g_picDeleteCLSID);
	commands.emplace_back();
	commands.emplace_back(eMoveUpValue, ibT("MoveUp"), _("Move up"), g_picMoveUpCLSID);
	commands.emplace_back(eMoveDownValue, ibT("MoveDown"), _("Move down"), g_picMoveDownCLSID);
	commands.emplace_back();
	commands.emplace_back(eSortAscValue, ibT("SortAsc"), _("Sort ascending"), g_picSortAscCLSID);
	commands.emplace_back(eSortDescValue, ibT("SortDesc"), _("Sort descending"), g_picSortDescCLSID);
}

void ibValueValueList::CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, ibBackendValueForm* /*srcForm*/)
{
	if (ibBackendException::IsEvalMode())
		return;
	switch (lNumAction) {
	case eAddValue: {
		const long at = StorageIndexOf(ctx.m_selection);
		if (at >= 0)
			ibValueModelStorage::Insert(NewRow(), static_cast<unsigned int>(at + 1));
		else
			ibValueModelStorage::Append(NewRow());
		break;
	}
	case eDeleteValue:
		if (ibComposerNode* node = StorageRowOf(ctx.m_selection))
			ibValueModelStorage::Remove(node);
		break;
	case eMoveUpValue: MoveRow(ctx.m_selection, -1); break;
	case eMoveDownValue: MoveRow(ctx.m_selection, +1); break;
	case eSortAscValue: if (ctx.m_column.IsOk()) SeatRows(ctx.m_column.m_name, true); break;
	case eSortDescValue: if (ctx.m_column.IsOk()) SeatRows(ctx.m_column.m_name, false); break;
	}
}

ibValue ibValueValueList::Add(const ibValue& value, const ibString& presentation, bool check, const ibValue& picture)
{
	return Insert(static_cast<unsigned int>(GetRowCount()), value, presentation, check, picture);
}

ibValue ibValueValueList::Insert(unsigned int index, const ibValue& value, const ibString& presentation, bool check, const ibValue& picture)
{
	if (index > static_cast<unsigned int>(GetRowCount()))
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	ibComposerNode* row = NewRow();
	row->SetValue(eValue, value);
	row->SetValue(ePresentation, ibValue(presentation));
	row->SetValue(eCheck, ibValue(check));
	row->SetValue(ePicture, picture);
	const bool notify = !ibBackendException::IsEvalMode();
	if (index == static_cast<unsigned int>(GetRowCount()))
		ibValueModelStorage::Append(row, notify);
	else
		ibValueModelStorage::Insert(row, index, notify);
	return GetRowAt(ibDataViewItem(row));
}

ibValue ibValueValueList::Get(unsigned int index)
{
	if (index >= static_cast<unsigned int>(GetRowCount()))
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	return GetRowAt(GetItem(index));
}

bool ibValueValueList::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	pvarValue = Get(varKeyValue.GetUInteger());
	return true;
}

ibValueModel::ibComposerNode* ibValueValueList::RowOf(const ibValue& item) const
{
	// An item is not a number. Reading it as an index answered 0, so Delete(item) removed the first row; a string
	// or Undefined does the same. So only an item or a number is taken.
	ibValueModelReturnLine* line = nullptr;
	if (item.ConvertToValue(line)) {
		const long at = IndexOf(item);
		if (at < 0)
			ibBackendCoreException::Error(_("The item is not in the list"));
		return Storage().GetNode(at);
	}
	if (item.GetType() != ibValueTypes::TYPE_NUMBER)
		ibBackendCoreException::Error(_("Expected an item or an index"));
	const unsigned int index = item.GetUInteger();
	if (index >= static_cast<unsigned int>(GetRowCount()))
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	return Storage().GetNode(index);
}

void ibValueValueList::Delete(const ibValue& item)
{
	ibComposerNode* node = RowOf(item);
	ibValueModelStorage::Remove(node, !ibBackendException::IsEvalMode());
}

void ibValueValueList::Move(const ibValue& item, long offset)
{
	ibComposerNode* node = RowOf(item);
	const long to = Storage().IndexOf(node) + offset;
	if (to < 0 || to >= GetRowCount())
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	if (offset != 0)
		ibValueModelStorage::Move(node, static_cast<int>(offset), !ibBackendException::IsEvalMode());
}

ibValue ibValueValueList::FindByValue(const ibValue& value)
{
	for (long row = 0; row < GetRowCount(); ++row) {
		ibComposerNode* node = Storage().GetNode(row);
		if (node != nullptr && node->GetTableValue(eValue).CompareValueEQ(value))
			return GetRowAt(ibDataViewItem(node));
	}
	return ibValue();
}

ibValue ibValueValueList::UnloadValues() const
{
	ibValueArray* const values = new ibValueArray();
	ibValue held = values;   // the wrapper owns it
	for (long row = 0; row < GetRowCount(); ++row)
		values->Add(Storage().GetCell(row, eValue));
	return held;
}

void ibValueValueList::LoadValues(const ibValue& source)
{
	ibValueArray* values = nullptr;
	if (!source.ConvertToValue(values))
		ibBackendCoreException::Error(_("LoadValues: the argument is not an array"));
	ibValueModelStorage::Clear(!ibBackendException::IsEvalMode());
	for (const ibValue& value : values->Values())
		Add(value, ibString(), false, ibValue());
}

ibValue ibValueValueList::Copy() const
{
	ibValuePtr<ibValueValueList> copy(new ibValueValueList());
	for (long row = 0; row < GetRowCount(); ++row)
		copy->Add(Storage().GetCell(row, eValue), Storage().GetCell(row, ePresentation).GetString(),
			Storage().GetCell(row, eCheck).GetBoolean(), Storage().GetCell(row, ePicture));
	return copy;
}

void ibValueValueList::FillChecks(bool check)
{
	for (long row = 0; row < GetRowCount(); ++row)
		if (ibComposerNode* node = Storage().GetNode(row))
			node->SetValue(eCheck, ibValue(check));
	NotifyReset();
}

bool ibValueValueList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAdd:
		pvarRetValue = Add(*paParams[0],
			lSizeArray > 1 ? paParams[1]->GetString() : paParams[0]->GetString(),
			lSizeArray > 2 ? paParams[2]->GetBoolean() : false,
			lSizeArray > 3 ? *paParams[3] : ibValue());
		return true;
	case enInsert:
		pvarRetValue = Insert(paParams[0]->GetUInteger(), *paParams[1],
			lSizeArray > 2 ? paParams[2]->GetString() : paParams[1]->GetString(),
			lSizeArray > 3 ? paParams[3]->GetBoolean() : false,
			lSizeArray > 4 ? *paParams[4] : ibValue());
		return true;
	case enCount: pvarRetValue = static_cast<unsigned int>(GetRowCount()); return true;
	case enGet: pvarRetValue = Get(paParams[0]->GetUInteger()); return true;
	case enDelete: Delete(*paParams[0]); return true;
	case enClear: ibValueModelStorage::Clear(!ibBackendException::IsEvalMode()); return true;
	case enFindByValue: pvarRetValue = FindByValue(*paParams[0]); return true;
	case enUnloadValues: pvarRetValue = UnloadValues(); return true;
	case enLoadValues: LoadValues(*paParams[0]); return true;
	case enCopy: pvarRetValue = Copy(); return true;
	case enSortByValue: SeatRows(ibT("Value"), !(lSizeArray > 0 && paParams[0]->GetBoolean())); return true;
	case enSortByPresentation: SeatRows(ibT("Presentation"), !(lSizeArray > 0 && paParams[0]->GetBoolean())); return true;
	case enFillChecks: FillChecks(paParams[0]->GetBoolean()); return true;
	case enMove: Move(*paParams[0], paParams[1]->GetInteger()); return true;
	case enIndex: pvarRetValue = static_cast<int>(IndexOf(*paParams[0])); return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
//               a form's data source                                 //
//////////////////////////////////////////////////////////////////////

const ibMetaData* ibValueValueList::GetSourceMetaData() const
{
	// A list belongs to no configuration; a reference it holds resolves in the active one, as a value table's does.
	return ibApplicationInstance::GetActiveMetaData();
}

const ibSourceDataObject::ibSourceExplorer* ibValueValueList::GetSourceExplorer() const
{
	// A TABLE, so an attribute of this type drops on a form as a table box and its columns as the box's columns.
	// The value is the column the list is about, and it is not shown unless asked for: a row is seen by its
	// presentation and its check.
	m_sourceExplorer.Reset(GetClassName(), GetClassName(), wxNOT_FOUND, g_valueListCLSID, /*tableSection*/true);
	for (unsigned int i = 0; i < m_columns->GetColumnCount(); ++i) {
		const ibValueValueListColumnCollection::ibValueValueListColumnInfo* const col =
			static_cast<const ibValueValueListColumnCollection::ibValueValueListColumnInfo*>(m_columns->GetColumnInfo(i));
		m_sourceExplorer.AppendColumn(col, static_cast<ibMetaID>(col->GetColumnID()), /*enabled*/true,
			/*visible*/col->GetColumnID() != eValue);
	}
	return &m_sourceExplorer;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueValueList, "ValueList", g_valueListCLSID);

SYSTEM_TYPE_REGISTER(ibValueValueList::ibValueValueListColumnCollection, "ValueListColumns", system_to_clsid("VL_VLCL"));
SYSTEM_TYPE_REGISTER(ibValueValueList::ibValueValueListColumnCollection::ibValueValueListColumnInfo, "ValueListColumn", system_to_clsid("VL_VLCI"));
SYSTEM_TYPE_REGISTER(ibValueValueList::ibValueValueListItem, "ValueListItem", g_valueListItemCLSID);
