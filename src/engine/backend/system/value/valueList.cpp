#include "valueList.h"

#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"

#include <algorithm>

enum { enItemValue = 0, enItemPresentation, enItemCheck };

void ibValueValueListItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Value"));
	helper.AppendProp(wxT("Presentation"));
	helper.AppendProp(wxT("Check"));
}

bool ibValueValueListItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enItemValue: pvarPropVal = m_value; return true;
	case enItemPresentation: pvarPropVal = m_presentation; return true;
	case enItemCheck: pvarPropVal = m_check; return true;
	}
	return false;
}

bool ibValueValueListItem::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enItemValue: m_value = varPropVal; return true;
	case enItemPresentation: m_presentation = varPropVal.GetString(); return true;
	case enItemCheck: m_check = varPropVal.GetBoolean(); return true;
	}
	return false;
}

enum { enAdd = 0, enInsert, enCount, enGet, enDelete, enClear, enFindByValue,
	enUnloadValues, enLoadValues, enCopy, enSortByValue, enSortByPresentation, enFillChecks, enMove, enIndex };

void ibValueValueList_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("ValueList()"));
	// Picture is accepted and ignored: a 1C call carries it, and refusing the argument stops the call.
	helper.AppendFunc(wxT("Add"), 4, wxT("Add(value, presentation, check, picture)"));
	helper.AppendFunc(wxT("Insert"), 5, wxT("Insert(index, value, presentation, check, picture)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index)"));
	helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(item)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("FindByValue"), 1, wxT("FindByValue(value)"));
	helper.AppendFunc(wxT("UnloadValues"), wxT("UnloadValues()"));
	helper.AppendFunc(wxT("LoadValues"), 1, wxT("LoadValues(array)"));
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("SortByValue"), 1, wxT("SortByValue(descending = false)"));
	helper.AppendFunc(wxT("SortByPresentation"), 1, wxT("SortByPresentation(descending = false)"));
	helper.AppendFunc(wxT("FillChecks"), 1, wxT("FillChecks(check)"));
	helper.AppendFunc(wxT("Move"), 2, wxT("Move(item, offset)"));
	helper.AppendFunc(wxT("Index"), 1, wxT("Index(item) : number"));
}

void ibValueValueList::CheckIndex(unsigned int index) const
{
	if (index >= m_items.size())
		ibBackendCoreException::Error(_("Index goes beyond the list"));
}

ibValuePtr<ibValueValueListItem> ibValueValueList::MakeItem(const ibValue& value, const ibString& presentation, bool check) const
{
	ibValuePtr<ibValueValueListItem> item(new ibValueValueListItem());
	item->m_value = value;
	item->m_presentation = presentation;
	item->m_check = check;
	return item;
}

ibValue ibValueValueList::Add(const ibValue& value, const ibString& presentation, bool check)
{
	m_items.push_back(MakeItem(value, presentation, check));
	return m_items.back();
}

ibValue ibValueValueList::Insert(unsigned int index, const ibValue& value, const ibString& presentation, bool check)
{
	if (index > m_items.size())
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	m_items.insert(m_items.begin() + index, MakeItem(value, presentation, check));
	return m_items[index];
}

ibValue ibValueValueList::Get(unsigned int index) const
{
	CheckIndex(index);
	return m_items[index];
}

void ibValueValueList::Delete(unsigned int index)
{
	CheckIndex(index);
	m_items.erase(m_items.begin() + index);
}

unsigned int ibValueValueList::Position(const ibValue& item) const
{
	// An item is not a number. Reading it as an index answered 0, so Delete(item)
	// always removed the first element. A string or Undefined does the same, so
	// only an item or a number is accepted.
	ibValueValueListItem* held = nullptr;
	if (item.ConvertToValue(held)) {
		const long at = Index(item);
		if (at < 0)
			ibBackendCoreException::Error(_("The item is not in the list"));
		return static_cast<unsigned int>(at);
	}
	if (item.GetType() != ibValueTypes::TYPE_NUMBER)
		ibBackendCoreException::Error(_("Expected an item or an index"));
	const unsigned int index = item.GetUInteger();
	CheckIndex(index);
	return index;
}

void ibValueValueList::Delete(const ibValue& item)
{
	Delete(Position(item));
}

ibValue ibValueValueList::FindByValue(const ibValue& value) const
{
	for (const ibValuePtr<ibValueValueListItem>& item : m_items)
		if (item->m_value.CompareValueEQ(value))
			return item;
	return ibValue();
}

ibValue ibValueValueList::UnloadValues() const
{
	ibValueArray* const values = new ibValueArray();
	for (const ibValuePtr<ibValueValueListItem>& item : m_items)
		values->Add(item->m_value);
	return values;
}

void ibValueValueList::LoadValues(const ibValue& source)
{
	ibValueArray* values = nullptr;
	if (!source.ConvertToValue(values))
		ibBackendCoreException::Error(_("LoadValues: the argument is not an array"));
	Clear();
	for (const ibValue& value : values->Values())
		Add(value, ibString(), false);
}

ibValue ibValueValueList::Copy() const
{
	ibValuePtr<ibValueValueList> copy(new ibValueValueList());
	for (const ibValuePtr<ibValueValueListItem>& item : m_items)
		copy->Add(item->m_value, item->m_presentation, item->m_check);
	return copy;
}

void ibValueValueList::SortByValue(bool descending)
{
	std::stable_sort(m_items.begin(), m_items.end(),
		[descending](const ibValuePtr<ibValueValueListItem>& a, const ibValuePtr<ibValueValueListItem>& b) {
			return descending ? b->m_value < a->m_value : a->m_value < b->m_value;
		});
}

void ibValueValueList::SortByPresentation(bool descending)
{
	std::stable_sort(m_items.begin(), m_items.end(),
		[descending](const ibValuePtr<ibValueValueListItem>& a, const ibValuePtr<ibValueValueListItem>& b) {
			// Compared as values are, so a list sorts its presentations as Array.Sort sorts strings.
			const ibValue left(a->m_presentation), right(b->m_presentation);
			return descending ? right < left : left < right;
		});
}

void ibValueValueList::FillChecks(bool check)
{
	for (const ibValuePtr<ibValueValueListItem>& item : m_items)
		item->m_check = check;
}

void ibValueValueList::Move(const ibValue& item, long offset)
{
	const long from = static_cast<long>(Position(item));
	const long to = from + offset;
	if (to < 0 || to >= static_cast<long>(m_items.size()))
		ibBackendCoreException::Error(_("Index goes beyond the list"));
	ibValuePtr<ibValueValueListItem> moved = m_items[from];
	m_items.erase(m_items.begin() + from);
	m_items.insert(m_items.begin() + to, moved);
}

long ibValueValueList::Index(const ibValue& item) const
{
	ibValueValueListItem* held = nullptr;
	if (item.ConvertToValue(held))
		for (size_t at = 0; at < m_items.size(); ++at)
			if (m_items[at] == held)
				return static_cast<long>(at);
	return -1;
}

void ibValueValueList::AppendValues(std::vector<ibValue>& out) const
{
	for (const ibValuePtr<ibValueValueListItem>& item : m_items)
		out.push_back(item->m_value);
}

bool ibValueValueList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAdd:
		pvarRetValue = Add(*paParams[0],
			lSizeArray > 1 ? paParams[1]->GetString() : paParams[0]->GetString(),
			lSizeArray > 2 ? paParams[2]->GetBoolean() : false);
		return true;
	case enInsert:
		pvarRetValue = Insert(paParams[0]->GetUInteger(), *paParams[1],
			lSizeArray > 2 ? paParams[2]->GetString() : paParams[1]->GetString(),
			lSizeArray > 3 ? paParams[3]->GetBoolean() : false);
		return true;
	case enCount: pvarRetValue = Count(); return true;
	case enGet: pvarRetValue = Get(paParams[0]->GetUInteger()); return true;
	case enDelete: Delete(*paParams[0]); return true;
	case enClear: Clear(); return true;
	case enFindByValue: pvarRetValue = FindByValue(*paParams[0]); return true;
	case enUnloadValues: pvarRetValue = UnloadValues(); return true;
	case enLoadValues: LoadValues(*paParams[0]); return true;
	case enCopy: pvarRetValue = Copy(); return true;
	case enSortByValue: SortByValue(lSizeArray > 0 && paParams[0]->GetBoolean()); return true;
	case enSortByPresentation: SortByPresentation(lSizeArray > 0 && paParams[0]->GetBoolean()); return true;
	case enFillChecks: FillChecks(paParams[0]->GetBoolean()); return true;
	case enMove: Move(*paParams[0], paParams[1]->GetInteger()); return true;
	case enIndex: pvarRetValue = static_cast<int>(Index(*paParams[0])); return true;
	}
	return false;
}

bool ibValueValueList::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	pvarValue = Get(varKeyValue.GetUInteger());
	return true;
}

std::shared_ptr<ibValueIteratorState> ibValueValueList::CreateIterator()
{
	class ListIteratorState : public ibValueIteratorState {
	public:
		explicit ListIteratorState(ibValueValueList* owner) : m_owner(owner) {
			if (m_owner != nullptr) m_owner->IncrRef();
		}
		~ListIteratorState() override {
			if (m_owner != nullptr) m_owner->DecrRef();
		}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_pos; else m_started = true;
			if (m_owner == nullptr || m_pos >= m_owner->Count()) return false;
			current = m_owner->Get(static_cast<unsigned int>(m_pos));
			return true;
		}
		void Reset() override { m_pos = 0; m_started = false; }
	private:
		ibValueValueList* m_owner;
		size_t m_pos = 0;
		bool m_started = false;
	};
	return std::make_shared<ListIteratorState>(this);
}

VALUE_TYPE_REGISTER(ibValueValueList, "ValueList", g_valueListCLSID);
VALUE_TYPE_REGISTER(ibValueValueListItem, "ValueListItem", g_valueListItemCLSID);
