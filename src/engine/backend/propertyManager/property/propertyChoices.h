#ifndef __PROPERTY_CHOICES_H__
#define __PROPERTY_CHOICES_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/description/choicesDescription.h"

// ⭐ THE ITEMS A SWITCH OFFERS — one property holding an ibChoicesDescription, as the choice parameters hold theirs: a
// row per item, the value it writes and the text it shows (Max, 2026-11-23: "a property where you set the values and
// the presentations"). A value is a node the application's door wrote (ibSerializeValue), so an enumeration's member
// is kept as itself; the presentation is a text in its stored form, every language at once. Edited in its own window
// (the desktop's ibDialogChoices).
//
// (Not "ChoiceList": ibPropertyChoiceList is the inspector's drop-down, propertyObject.h.)
class BACKEND_API ibPropertyChoices : public ibProperty {
	static ibVariant CreateVariantData(ibPropertyObject* property, const ibChoicesDescription& choicesDesc = ibChoicesDescription());
public:

	ibChoicesDescription& GetValueAsChoicesDesc() const;
	// …of the field's type: a list left from another type is cleared.
	void SetValue(const ibChoicesDescription& val);

	ibPropertyChoices(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// NO ITEMS — the switch offers nothing.
	virtual bool IsEmptyProperty() const override { return !GetValueAsChoicesDesc().IsOk(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	// The family rule — a value arrives in whichever wrapper the caller was handed. See propertyRecord.h.
	virtual void DoSetValue(const ibVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// …and what its window asks of the application: Type — the type of the field the switch stands in, as a type is
	// written (what a value typed into a row is made of); Values — where that type is finite (a Boolean, an
	// enumeration), its values, each its Value as a row stores it and its Presentation.
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;
};

// AN ITEM'S TEXT — its presentation in the language in force, or, none of its own, its value's, read against the
// application (an enumeration's member by its name); an empty value says "<empty>". The switch's button and the
// inspector's row say the same.
BACKEND_API ibString ibChoiceCaption(const ibChoiceDescription& row, const ibMetaData* metaData);

// A TYPE THE ITEMS CAN BE OF — a number, a string, a date, a Boolean, an enumeration's member (the engine's or the
// application's): what a switch is offered for, its type being one of these alone.
BACKEND_API bool ibIsChoiceType(const ibClassID& clsid);

#endif
