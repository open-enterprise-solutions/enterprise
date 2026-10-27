#ifndef __PROPERTY_FORM_H__
#define __PROPERTY_FORM_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "form"
class BACKEND_API ibPropertyForm : public ibProperty {
	static ibVariant CreateVariantData();
public:

	ibString GetValueAsString() const;
	ibMemoryBuffer& GetValueAsMemoryBuffer() const;
	void SetValue(const ibString& val);
	void SetValue(const ibMemoryBuffer& val);

	ibPropertyForm(ibPropertyCategory* cat, const ibString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertyForm(ibPropertyCategory* cat, const ibString& name, const ibString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertyForm(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	//copy & paste — pulls the LIVE form data (not the stored buffer)
	virtual bool CopyNodeValue(ibDataValue& value) const override;
	virtual bool PasteNodeValue(const ibDataValue& value) override;

	// …and what its link opens: the form it is the data of (Object).
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

	// 🛑 AND THE VALUE IS NEVER TAKEN OFF A BARE VARIANT HERE. This cell holds TWO things — the
	// module text AND the form data — so "the value" has no single reading: taken as a string it
	// would write the text and silently drop the control tree beside it. Whoever writes says which
	// half they mean, with SetValue(wxString) or SetValue(buffer).
public:

};

#endif