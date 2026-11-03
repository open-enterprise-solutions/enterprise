#ifndef __PROPERTY_TEMPLATE_H__
#define __PROPERTY_TEMPLATE_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/description/templateDescription.h"

// base property for "template" — what it holds, of its kind (core/description/templateDescription.h).
//
// ⭐ A KIND IS CHOSEN ONCE. It is offered while none is (GetValueList), every kind under every owner; once chosen
// there is nothing to choose — a kind replaced would drop the schema with it.
class BACKEND_API ibPropertyTemplate : public ibProperty {
	static ibVariant CreateVariantData(const ibTemplateDescription& val = ibTemplateDescription());
public:

	ibTemplateDescription& GetValueAsTemplateDesc() const;
	void SetValue(const ibTemplateDescription& val);

	ibPropertyTemplate(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	// No kind chosen yet.
	virtual bool IsEmptyProperty() const override;

	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// set/get property data — a script is handed a value of its kind
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// …and what its link opens: the template it is the description of (Object).
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

protected:

	//load & save object in control
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif
