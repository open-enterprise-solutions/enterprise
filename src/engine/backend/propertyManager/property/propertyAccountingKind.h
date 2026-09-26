#ifndef __PROPERTY_ACCOUNTING_KIND_H__
#define __PROPERTY_ACCOUNTING_KIND_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

#include <functional>   // std::function — the way to the owner's chart, handed in

class ibValueMetaObjectChartOfAccounts;

// ⭐⭐ WHICH KIND OF ACCOUNTING THIS FIGURE BELONGS TO. A register's dimension or resource names one of
// the kinds declared by the CHART the register stands on: the currency amount belongs to currency
// accounting, the quantity to quantity accounting — and on an account that does not keep that kind the
// figure is not zero but absent.
//
// 🛑 THE LIST IS THIS REGISTER'S CHART'S, AND NOT EVERY CHART'S. The shared CreateValueList walks the
// whole configuration by clsid, which here would offer the kinds of a chart this register has nothing
// to do with — a choice that can mean nothing and would be stored all the same.
//
// ⭐⭐ AND THE CHART IS HANDED IN, NOT LOOKED FOR. The field that declares this property — a dimension,
// a resource — is the one that knows what it stands under; a property that went looking would have to
// work out the type of its owner and of its owner's owner at run time, which is discovery by cast and
// exactly what this engine does not do. So the owner passes the way to reach its chart when it creates
// the property, and this class only asks.
//
// The LEVEL decides which of the chart's two lists is offered — what the ACCOUNT is kept in, or what
// each of its breakdowns is. One class, because the difference is the list and nothing else.
class BACKEND_API ibPropertyAccountingKind : public ibProperty {
public:

	enum class Level { Account, AccountDimension };

private:

	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription());
	Level m_level = Level::Account;
	// What the owner handed over: "my chart of accounts, or null while I stand on none".
	std::function<const ibValueMetaObjectChartOfAccounts* ()> m_chartOfAccounts;

public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyAccountingKind(ibPropertyCategory* cat, Level level,
		std::function<const ibValueMetaObjectChartOfAccounts* ()> chartOfAccounts,
		const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject()))
		, m_level(level), m_chartOfAccounts(std::move(chartOfAccounts)) {}

	// NOTHING CHOSEN — the ordinary state. A figure that belongs to no particular kind of accounting is
	// kept for every account, which is what an amount is.
	virtual bool IsEmptyProperty() const override { return GetValueAsMetaDesc().GetTypeCount() == 0; }

	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	// The family rule — a relationship arrives in whichever wrapper the caller was handed. See
	// propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif // __PROPERTY_ACCOUNTING_KIND_H__
