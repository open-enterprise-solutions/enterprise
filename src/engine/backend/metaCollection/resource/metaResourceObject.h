#ifndef _RESOURCE_H__
#define _RESOURCE_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/propertyManager/property/propertyAccountingKind.h"

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

	// ⭐ AND WHICH KINDS OF ACCOUNTING IT BELONGS TO — two questions, because they are asked of two
	// different things. The ACCOUNT's kind decides whether the figure exists for this account at all (a
	// currency amount on a hryvnia account does not); the BREAKDOWN's decides which of the account's
	// breakdowns it is kept by (a quantity per item, not per contract). Empty means "always", which is
	// what an amount is.
	ibMetaDescription& GetAccountingKind() const { return m_propertyAccountingKind->GetValueAsMetaDesc(); }
	ibMetaDescription& GetAccountDimensionAccountingKind() const { return m_propertyAccountDimensionAccountingKind->GetValueAsMetaDesc(); }

	// MY CHART OF ACCOUNTS, OR NONE — the same question a dimension asks, and for the same reason: a
	// resource belongs to every register kind, so it asks its owner what it IS rather than assuming.
	const class ibValueMetaObjectChartOfAccounts* GetChartOfAccounts() const;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//get data selector
	virtual ibSelectorDataType GetFilterDataType() const;

	virtual void OnPropertyRefresh() override;
	virtual bool OnDeleteMetaObject() override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// Default TRUE: the amount is what a posting is, and it is the resource an accounting register is
	// declared for first. A quantity is the exception and its author says so — which is the way round
	// that leaves the check working out of the box instead of silently doing nothing until somebody
	// finds the switch.
	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));
	ibPropertyAccountingKind* m_propertyAccountingKind = ibPropertyObject::CreateProperty<ibPropertyAccountingKind>(
		m_categoryAccounting, ibPropertyAccountingKind::Level::Account,
		[this] { return GetChartOfAccounts(); },
		wxT("AccountingKind"), _("Accounting kind"),
		_("Accounting registers only: the kind of accounting this figure belongs to. Empty - kept for every account; named - kept only for accounts that keep that kind, and absent on the rest."));

	ibPropertyAccountingKind* m_propertyAccountDimensionAccountingKind = ibPropertyObject::CreateProperty<ibPropertyAccountingKind>(
		m_categoryAccounting, ibPropertyAccountingKind::Level::AccountDimension,
		[this] { return GetChartOfAccounts(); },
		wxT("AccountDimensionAccountingKind"), _("Account dimension accounting kind"),
		_("Accounting registers only: the breakdown's kind of accounting this figure is kept BY. Empty - kept for the account as a whole; named - kept against each breakdown that keeps that kind."));

	ibPropertyBoolean*  m_propertyBalance = ibPropertyObject::CreateProperty<ibPropertyBoolean>(
		m_categoryAccounting, wxT("Balance"), _("Balance"),
		_("Accounting registers only: whether this figure holds ONE value for the whole entry, which the two sides must agree on. On by default - the amount is what a posting is. Cleared, it is kept per side - a debit figure and a credit figure - which is what a quantity or an amount in currency is: three pieces left one account and an amount in dollars reached the other."),
		true);
};

#endif