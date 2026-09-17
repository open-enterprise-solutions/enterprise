#ifndef __ACCOUNTING_KIND_OBJECT_H__
#define __ACCOUNTING_KIND_OBJECT_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"

// ⭐⭐ WHAT KIND OF ACCOUNTING AN ACCOUNT IS KEPT IN — currency, quantity, tax. The chart declares the
// kinds it knows; every account then says yes or no to each, which is why this is an ATTRIBUTE and
// always a boolean one: the author writes it like any other field, and the account answers it.
//
// What makes it a kind of METAOBJECT of its own is who reads it. A register's dimension and its
// resources name one, and the figure they carry exists only for accounts that keep that kind of
// accounting: a currency amount on a hryvnia account is not zero, it is ABSENT. That is a list to
// choose from ("which kind of accounting is this figure part of"), which a boolean attribute among
// fifty others could never be.
//
// ⚠ ITS TYPE IS NOT A CHOICE. Boolean is what the metatype MEANS; the property is hidden rather than
// defaulted, because a kind holding a string would be read by nobody and refused by nothing.
class BACKEND_API ibValueMetaObjectAccountingKind : public ibValueMetaObjectAttribute {
public:

	ibValueMetaObjectAccountingKind() : ibValueMetaObjectAttribute(ibValueTypes::TYPE_BOOLEAN) {}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//get data selector
	virtual ibSelectorDataType GetFilterDataType() const;

	virtual void OnPropertyRefresh() override;
};

// ⭐ THE SAME QUESTION, ASKED OF ONE BREAKDOWN. An account's dimension-kinds table is where it says
// what it is broken down BY; this kind adds a tick-box column to that table, so the answer is given
// per breakdown — "a quantity is kept by item, not by contract". It differs from the one above in
// nothing but where its column lands, which is the owner's business, so it states only its identity.
class BACKEND_API ibValueMetaObjectAccountDimensionAccountingKind : public ibValueMetaObjectAccountingKind {
public:

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();
};

#endif // __ACCOUNTING_KIND_OBJECT_H__
