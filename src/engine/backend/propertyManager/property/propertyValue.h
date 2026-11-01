#ifndef __PROPERTY_VALUE_H__
#define __PROPERTY_VALUE_H__

#include "backend/propertyManager/propertyObject.h"

class ibPropertySource;

// ⭐ A VALUE OF THE TYPE A BINDING HOLDS — what a radio button stands for among the values of its Source: a number, a
// string, a date, a boolean, a member of an enumeration. The type is never its own: it is asked of the source property
// it follows, so the value is read and checked as the field it will be written into.
//
// HELD AS THE VALUE ITSELF (the variant a primitive or a reference is), and SAVED AS ONE: a primitive as its own typed
// scalar, anything else as the value's node through the configuration's door (ibMetaData::Serialize) — a member of an
// enumeration by its type and guid, never by a number standing for it.
//
// A boolean and an enumeration are CHOSEN (GetValueList: True / False, or the members); any other type takes a scalar
// of its own shape, adjusted to the source's type.
//
// ⚠ A VALUE THAT CANNOT BE READ RAISES (ibCoreException): a member deleted since, a word that names none of the
// choices. Never a quiet empty — a button standing for no value looks exactly like one nobody set.
class BACKEND_API ibPropertyValue : public ibProperty {
public:

	ibValue GetValueAsValue() const;
	void SetValue(const ibValue& value);

	ibPropertyValue(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibPropertySource* typedBy)
		: ibProperty(cat, name, label, helpString, ibVariant()), m_typedBy(typedBy)
	{
	}

	// No value chosen.
	virtual bool IsEmptyProperty() const override { return m_propValue.GetType() == ibValueTypes::TYPE_EMPTY; }

	// The members of the source's enumeration, or True and False — when the source holds exactly one such type.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	const ibPropertySource* m_typedBy;
};

#endif
