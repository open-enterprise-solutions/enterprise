#ifndef __PROPERTY_STRING_H__
#define __PROPERTY_STRING_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_localization.h"   // ibTranslateString — what a caption holds

class BACKEND_API ibPropertyStringBase : public ibProperty {
public:

	wxString GetValueAsString() const {
		static thread_local wxString result;
		GetValueAsString(result);
		return std::move(result);
	}

	bool GetValueAsString(wxString& result) const {
		if (!m_propValue.IsNull())
			return m_propValue.GetData()->Write(result);
		return false;
	}

	void SetValue(const wxString& strValue) { m_propValue = strValue; }

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibProperty(cat, name, value)
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibProperty(cat, name, label, value)
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibProperty(cat, name, label, helpString, value)
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

	ibPropertyString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

//base property for "general" - unique name 
class BACKEND_API ibPropertyUString : public ibPropertyStringBase {
public:

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

//base property for "general" - unique name or empty value 
class BACKEND_API ibPropertyUEString : public ibPropertyUString {
public:

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyUString(cat, name, value)
	{
	}

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyUString(cat, name, label, value)
	{
	}

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyUString(cat, name, label, helpString, value)
	{
	}

};

//base property for "caption" - for translate
// ⭐ IT HOLDS AN ibTranslateString, the way the number property holds an ibNumber — so it stands on
// ibProperty and not on the string base. A string property stores a string and its setter replaces
// it, which is exactly how every other language used to be lost.
class BACKEND_API ibPropertyTString : public ibProperty {
	wxVariantData* CreateVariantData(const ibTranslateString& translate) const;
public:

	ibTranslateString& GetValueAsTranslate() const;
	void SetValue(const ibTranslateString& translate);

	// THE ACTIVE SYNONYM — the text, in the language in force. It is the translation converting
	// itself, and it is the one question a label, a tooltip or a page header ever asks.
	wxString GetValueAsTranslateString() const { return GetValueAsTranslate(); }

	// …AND THE RAW TEMPLATE — `en = 'Goods'; ru = 'Товары';`, every language at once, as it is
	// written down. What a template cell keeps, and what a file is written with.
	//
	// ⚠ NAMED "RAW", not "String". Called GetValueAsString it is indistinguishable from the reader
	// every other string property has, and two callers asked for it while meaning the text — so a
	// person was shown `en = 'Title';` in a notebook page header, and the rename gate compared a
	// synonym against a template.
	wxString GetValueAsRawString() const { return GetValueAsTranslate().GetRawText(); }

	// ⚠ NO DEFAULT FOR THE VALUE. The three differ only by how many STRINGS precede it, so a default
	// makes `(cat, name, text)` fit the second one as well — `C2668: ambiguous call` at every
	// declaration (measured on the first build). The number property can afford one; this cannot.
	ibPropertyTString(ibPropertyCategory* cat, const wxString& name,
		const ibTranslateString& value) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const ibTranslateString& value) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
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

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name, value)
	{
	}

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label, value)
	{
	}

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString, value)
	{
	}

};

#endif
