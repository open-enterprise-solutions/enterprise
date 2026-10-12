#include "valueList.h"

#include "backend/backend_exception.h"

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

enum { enAdd = 0, enInsert, enCount, enGet, enDelete, enClear, enFindByValue };

void ibValueValueList_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("ValueList()"));
	// Picture is accepted and ignored: a 1C call carries it, and refusing the argument stops the call.
	helper.AppendFunc(wxT("Add"), 4, wxT("Add(value, presentation, check, picture)"));
	helper.AppendFunc(wxT("Insert"), 5, wxT("Insert(index, value, presentation, check, picture)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index)"));
	helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(index)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("FindByValue"), 1, wxT("FindByValue(value)"));
}

void ibValueValueList::CheckIndex(unsigned int index) const
{
	if (index >= m_items.size())
		ibBackendCoreException::Error(_("Index goes beyond the list"));
}

ibValue ibValueValueList::MakeItem(const ibValue& value, const ibString& presentation, bool check) const
{
	ibValueValueListItem* const item = new ibValueValueListItem();
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

ibValue ibValueValueList::FindByValue(const ibValue& value) const
{
	for (const ibValue& held : m_items) {
		const ibValueValueListItem* const item = dynamic_cast<const ibValueValueListItem*>(held.GetRef());
		if (item != nullptr && item->m_value.CompareValueEQ(value))
			return held;
	}
	return ibValue();
}

void ibValueValueList::AppendValues(std::vector<ibValue>& out) const
{
	for (const ibValue& held : m_items) {
		const ibValueValueListItem* const item = dynamic_cast<const ibValueValueListItem*>(held.GetRef());
		if (item != nullptr)
			out.push_back(item->m_value);
	}
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
	case enDelete: Delete(paParams[0]->GetUInteger()); return true;
	case enClear: Clear(); return true;
	case enFindByValue: pvarRetValue = FindByValue(*paParams[0]); return true;
	}
	return false;
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
