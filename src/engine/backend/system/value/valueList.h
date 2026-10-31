#ifndef __VALUE_LIST_H__
#define __VALUE_LIST_H__

#include "backend/compiler/value.h"

#include <vector>

constexpr ibClassID g_valueListCLSID = value_to_clsid("VL_VLST");
constexpr ibClassID g_valueListItemCLSID = value_to_clsid("VL_VLIT");

void ibValueValueListItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
void ibValueValueList_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueValueListItem : public ibValueStaticMembers<&ibValueValueListItem_BindNames> {
public:
	ibValueValueListItem() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	virtual ibClassID GetClassType() const override { return g_valueListItemCLSID; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	ibValue m_value;
	ibString m_presentation;
	bool m_check = false;
};

class BACKEND_API ibValueValueList : public ibValueStaticMembers<&ibValueValueList_BindNames> {
public:
	ibValueValueList() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	virtual ibClassID GetClassType() const override { return g_valueListCLSID; }
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;
	// list[index] reads the item there, as Get does. An item is changed through its properties.
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;
	ibValue Add(const ibValue& value, const ibString& presentation, bool check);
	ibValue Insert(unsigned int index, const ibValue& value, const ibString& presentation, bool check);
	unsigned int Count() const { return static_cast<unsigned int>(m_items.size()); }
	ibValue Get(unsigned int index) const;
	void Delete(unsigned int index);
	// An item removes that item. A number is an index. Anything else is refused.
	void Delete(const ibValue& item);
	void Clear() { m_items.clear(); }
	ibValue FindByValue(const ibValue& value) const;
	ibValue UnloadValues() const;
	void LoadValues(const ibValue& source);
	// A new list with new items holding the same value, presentation and check.
	ibValue Copy() const;
	// A stable sort: items that compare equal keep their order.
	void SortByValue(bool descending);
	void SortByPresentation(bool descending);
	void FillChecks(bool check);
	// The item (or the item at that index) moves by offset places; a negative offset moves it up.
	void Move(const ibValue& item, long offset);
	// The position of an item (0-based), as Table.Index answers it for a row; -1 for anything else.
	long Index(const ibValue& item) const;
	void AppendValues(std::vector<ibValue>& out) const;
private:
	void CheckIndex(unsigned int index) const;
	// Where an item of this list sits, or the index a number names. Anything else is refused.
	unsigned int Position(const ibValue& item) const;
	ibValuePtr<ibValueValueListItem> MakeItem(const ibValue& value, const ibString& presentation, bool check) const;
	std::vector<ibValuePtr<ibValueValueListItem>> m_items;
};

#endif
