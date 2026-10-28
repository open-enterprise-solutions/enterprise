#ifndef __PROPERTY_CHOICE_MODE_H__
#define __PROPERTY_CHOICE_MODE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : HOW MANY OF A PROPERTY'S CHOICES ARE HELD - one vocabulary for every side
////////////////////////////////////////////////////////////////////////////
//
// The list a property offers is each side's (propertyObject.h: ibPropertyChoiceList, ibBackendProperty::
// GetValueList); how many of it may be held is one answer, so it lives alone here, as selectorDataType.h does.

// HOW MANY OF THE CHOICES MAY BE HELD AT ONCE. Today this is folklore: the chart-of-characteristic-
// types property states "exactly one" in a comment while its editor is a multi-select tree, and a
// caller has no way to find out which is true. Said as a value, it is checkable — and a gate that
// knows it no longer needs per-call flags telling it whether to replace or to add.
enum class ibPropertyChoiceMode {
	None,     // not chosen from a list at all — a string, a number, a date. The default.
	Single,   // exactly one of them
	Mult,     // any number of them, the value being the set
};

#endif // __PROPERTY_CHOICE_MODE_H__
