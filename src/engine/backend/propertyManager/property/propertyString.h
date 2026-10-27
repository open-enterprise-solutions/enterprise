#ifndef __PROPERTY_STRING_H__
#define __PROPERTY_STRING_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_localization.h"   // ibTranslateString — what a caption holds

class BACKEND_API ibPropertyStringBase : public ibProperty {
public:

	ibString GetValueAsString() const {
		static thread_local ibString result;
		GetValueAsString(result);
		return std::move(result);
	}

	bool GetValueAsString(ibString& result) const {
		if (m_propValue.GetType() == ibValueTypes::TYPE_EMPTY)
			return false;
		result = m_propValue.GetString();
		return true;
	}

	void SetValue(const ibString& strValue) { m_propValue = ibVariant(strValue); }

	ibPropertyStringBase(ibPropertyCategory* cat, const ibString& name,
		const ibString& value) : ibProperty(cat, name, ibVariant(value))
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibString& value) : ibProperty(cat, name, label, ibVariant(value))
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibString& value) : ibProperty(cat, name, label, helpString, ibVariant(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsString().IsEmpty(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control

	// readable node value (typed String) — name / synonym / comment, etc.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

//base property for "string"
class BACKEND_API ibPropertyString : public ibPropertyStringBase {
public:

	ibPropertyString(ibPropertyCategory* cat, const ibString& name,
		const ibString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyString(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyString(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

//base property for "general" - unique name 
class BACKEND_API ibPropertyUString : public ibPropertyStringBase {
public:

	ibPropertyUString(ibPropertyCategory* cat, const ibString& name,
		const ibString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyUString(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyUString(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

//base property for "general" - unique name or empty value 
class BACKEND_API ibPropertyUEString : public ibPropertyUString {
public:

	ibPropertyUEString(ibPropertyCategory* cat, const ibString& name,
		const ibString& value) : ibPropertyUString(cat, name, value)
	{
	}

	ibPropertyUEString(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibString& value) : ibPropertyUString(cat, name, label, value)
	{
	}

	ibPropertyUEString(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibString& value) : ibPropertyUString(cat, name, label, helpString, value)
	{
	}

};

//base property for "caption" - for translate
// ⭐ IT HOLDS AN ibTranslateString, the way the number property holds an ibNumber — so it stands on
// ibProperty and not on the string base. A string property stores a string and its setter replaces
// it, which is exactly how every other language used to be lost.
class BACKEND_API ibPropertyTString : public ibProperty {
	// 🛑 STATIC, BECAUSE EVERY CALLER IS A MEMBER-INITIALISER. The three constructors below pass its
	// result to the base, which means it runs BEFORE this object's lifetime has begun; as a non-static
	// member that is a call on an object that does not exist yet, and UBSan says so in those words -
	// "member call on address which does not point to an object of type 'ibPropertyTString'", 98 times
	// in one run (2026-09-22). It never needed `this`: it builds a variant out of its argument.
	// ⚠ The same shape lives in the other property headers of this family - they are not converted
	// here, because only the one the tests exercise has been measured.
	static ibVariant CreateVariantData(const ibTranslateString& translate);
public:

	ibTranslateString& GetValueAsTranslate() const;
	void SetValue(const ibTranslateString& translate);

	// THE ACTIVE SYNONYM — the text, in the language in force. It is the translation converting
	// itself, and it is the one question a label, a tooltip or a page header ever asks.
	ibString GetValueAsTranslateString() const { return GetValueAsTranslate().GetString(); }

	// …AND THE RAW TEMPLATE — `en = 'Goods'; de = 'Waren';`, every language at once, as it is
	// written down. What a template cell keeps, and what a file is written with.
	//
	// ⚠ NAMED "RAW", not "String". Called GetValueAsString it is indistinguishable from the reader
	// every other string property has, and two callers asked for it while meaning the text — so a
	// person was shown `en = 'Title';` in a notebook page header, and the rename gate compared a
	// synonym against a template.
	ibString GetValueAsRawString() const { return GetValueAsTranslate().GetRawText(); }

	// ⚠ NO DEFAULT FOR THE VALUE. The three differ only by how many STRINGS precede it, so a default
	// makes `(cat, name, text)` fit the second one as well — `C2668: ambiguous call` at every
	// declaration (measured on the first build). The number property can afford one; this cannot.
	ibPropertyTString(ibPropertyCategory* cat, const ibString& name,
		const ibTranslateString& value) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibTranslateString& value) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibTranslateString& value) : ibProperty(cat, name, label, helpString, CreateVariantData(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsTranslate().IsEmpty(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

//base property for "text"
class BACKEND_API ibPropertyMString : public ibPropertyStringBase {
public:

	ibPropertyMString(ibPropertyCategory* cat, const ibString& name,
		const ibString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyMString(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyMString(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

#endif
