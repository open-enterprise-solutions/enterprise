#ifndef _DIMENSION_H__
#define _DIMENSION_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/propertyManager/property/propertyAccountingKind.h"

class BACKEND_API ibValueMetaObjectDimension : public ibValueMetaObjectAttribute {
	public:

	// ⭐⭐ IS THIS DIMENSION THE SAME ON BOTH SIDES OF AN ENTRY? Balanced means one value for the whole
	// posting — the organisation a line belongs to is the organisation on either side of it. Cleared,
	// the dimension is kept per SIDE: a debit value and a credit value, the way the account and its
	// breakdowns already are, which is what a currency or a tax purpose needs when the two sides of one
	// entry are kept differently.
	bool IsBalanceDimension() const { return m_propertyBalance->GetValueAsBoolean(); }

	// …AND WHICH KIND OF ACCOUNTING IT BELONGS TO. Empty means "every account"; named, the dimension is
	// filled only where the account keeps that kind — a currency on a currency account, and nothing on
	// the others.
	ibMetaDescription& GetAccountingKind() const { return m_propertyAccountingKind->GetValueAsMetaDesc(); }

	// MY CHART OF ACCOUNTS, OR NONE. A dimension belongs to every register kind, so it ASKS its owner
	// what it is — the class id, which the owner answers for itself — and only then reads the chart off
	// it, by the name of the one type that has one. Null where the owner is any other register, which
	// is what the two properties above are hidden for.
	const class ibValueMetaObjectChartOfAccounts* GetChartOfAccounts() const;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual void OnPropertyRefresh() override;
	virtual bool OnDeleteMetaObject() override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// ⚠ DECLARED FOR EVERY DIMENSION, SHOWN WHERE IT MEANS SOMETHING. A dimension belongs to every
	// register kind, and only an accounting register has two sides for a value to differ across — so
	// both properties are hidden unless the owner is one. The same rule the resource's Balance follows.
	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));

	// Default TRUE: one value per entry is what a dimension IS until somebody says the two sides differ.
	ibPropertyBoolean* m_propertyBalance = ibPropertyObject::CreateProperty<ibPropertyBoolean>(
		m_categoryAccounting, wxT("Balance"), _("Balance"),
		_("Accounting registers only: whether this dimension holds ONE value for the whole entry. Cleared, it is kept per side - a debit value and a credit value."),
		true);

	ibPropertyAccountingKind* m_propertyAccountingKind = ibPropertyObject::CreateProperty<ibPropertyAccountingKind>(
		m_categoryAccounting, ibPropertyAccountingKind::Level::Account,
		[this] { return GetChartOfAccounts(); },   // the chart is handed over, not looked for
		wxT("AccountingKind"), _("Accounting kind"),
		_("Accounting registers only: the kind of accounting this dimension belongs to. Empty - filled for every account; named - filled only for accounts that keep that kind, and absent on the rest."));
};

#endif
