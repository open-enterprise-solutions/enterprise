#ifndef __PROPERTY_META_OBJECT_CHOICE_H__
#define __PROPERTY_META_OBJECT_CHOICE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

// ONE OBJECT OF THE CONFIGURATION, OF ONE KIND — a document's numerator, a register's chart. The kind is
// the property's argument rather than its class: what the candidates are is a clsid, and a class per
// clsid was a copy per clsid. Holds an ibMetaDescription (at most one entry), so every verb that binds a
// relationship (metadata_bind, the inspector's drop-down) reaches it with no word of its own.
//
// (The chart properties — ibPropertyChartOfAccounts and its two siblings — are the same shape and move
// onto this one in a change of their own.)
class BACKEND_API ibPropertyMetaObjectChoice : public ibProperty {
	static ibVariant CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription());
public:

	ibPropertyMetaObjectChoice(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibClassID& kind)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())), m_kind(kind) {}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// NOTHING CHOSEN — asked of the property itself.
	virtual bool IsEmptyProperty() const override { return GetValueAsMetaDesc().GetTypeCount() == 0; }

	// The objects of the kind in this configuration; exactly one may be chosen.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	const ibClassID& GetKind() const { return m_kind; }

protected:

	// The family rule (propertyRecord.h): a relationship arrives in whichever wrapper the caller was
	// handed, and a property that stores a neighbour's wrapper raises on every later read.
	virtual void DoSetValue(const ibVariant& val) override;

private:

	const ibClassID m_kind;
};

#endif
