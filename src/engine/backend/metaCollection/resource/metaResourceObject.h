#ifndef _RESOURCE_H__
#define _RESOURCE_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"

class BACKEND_API ibValueMetaObjectResource : public ibValueMetaObjectAttribute {
	public:

	ibValueMetaObjectResource() : ibValueMetaObjectAttribute(ibValueTypes::TYPE_NUMBER) {
	}

	// ⭐⭐ IS THIS THE FIGURE THE ENTRY BALANCES ON? An accounting question, asked of the resource
	// because the resource is the only thing that can answer it.
	//
	// A posting balances in ONE figure — the amount — and not in the others: a quantity legitimately
	// differs between the two sides (ten items received against one invoice line), and a currency
	// amount balances only within its own currency. So "debits equal credits" is a statement about a
	// NAMED resource, and a check written against every numeric one would refuse perfectly good
	// postings. It also decides which figure a balance is READ in on both sides.
	//
	// ⚠ DECLARED HERE, SHOWN ONLY WHERE IT MEANS SOMETHING. A resource belongs to every register kind,
	// and an accumulation register has no sides to balance — so the property is hidden unless the owner
	// is an accounting register (OnPropertyRefresh, the same rule SelectMode and ItemMode follow: the
	// question belongs to the OWNER). One class, no second resource metatype, no hand-written list of
	// clsids for the walks to disagree over.
	bool IsBalanceResource() const { return m_propertyBalance->GetValueAsBoolean(); }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//get data selector
	virtual ibSelectorDataType GetFilterDataType() const;

	virtual void OnPropertyRefresh() override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// Default TRUE: the amount is what a posting is, and it is the resource an accounting register is
	// declared for first. A quantity is the exception and its author says so — which is the way round
	// that leaves the check working out of the box instead of silently doing nothing until somebody
	// finds the switch.
	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));
	ibPropertyBoolean*  m_propertyBalance = ibPropertyObject::CreateProperty<ibPropertyBoolean>(
		m_categoryAccounting, wxT("Balance"), _("Balance"), true);
};

#endif