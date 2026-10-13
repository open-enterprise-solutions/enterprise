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
	ibValue Add(const ibValue& value, const ibString& presentation, bool check);
	ibValue Insert(unsigned int index, const ibValue& value, const ibString& presentation, bool check);
	unsigned int Count() const { return static_cast<unsigned int>(m_items.size()); }
	ibValue Get(unsigned int index) const;
	void Delete(unsigned int index);
	// An item removes that item. A number is an index. Anything else is refused.
	void Delete(const ibValue& item);
	// Where this item sits, or -1 when it is not one of this list's items.
	long IndexOf(const ibValue& item) const;
	void Clear() { m_items.clear(); }
	ibValue FindByValue(const ibValue& value) const;
	ibValue UnloadValues() const;
	void LoadValues(const ibValue& source);
	void AppendValues(std::vector<ibValue>& out) const;
private:
	void CheckIndex(unsigned int index) const;
	ibValue MakeItem(const ibValue& value, const ibString& presentation, bool check) const;
	std::vector<ibValue> m_items;
};

#endif
