#ifndef __PROPERTY_STRING_H__
#define __PROPERTY_STRING_H__

#include "frmclient/backend/propertyManager/property/propertySerializable.h"
#include "frmclient/backend/backend_localization.h"   // ibTranslateString — what a caption holds

class FRMCLIENT_API ibPropertyStringBase : public ibPropertySerializable {
public:

	// …held as its node (ibPropertySerializable::ReadNodeValue) — set by the string it is.
	wxString GetValueAsString() const;
	void SetValue(const wxString& strValue);

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name) : ibPropertySerializable(cat, name, wxNullVariant)
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibPropertySerializable(cat, name, label, wxNullVariant)
	{
	}

	ibPropertyStringBase(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsString().IsEmpty(); }

	//load & save object in control

	// readable node value (typed String) — name / synonym / comment, etc.
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

//base property for "string"
class FRMCLIENT_API ibPropertyString : public ibPropertyStringBase {
public:

	ibPropertyString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name)
	{
		SetValue(value);
	}

	ibPropertyString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label)
	{
		SetValue(value);
	}

	ibPropertyString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString)
	{
		SetValue(value);
	}

};

//base property for "general" - unique name
class FRMCLIENT_API ibPropertyUString : public ibPropertyStringBase {
public:

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name)
	{
		SetValue(value);
	}

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label)
	{
		SetValue(value);
	}

	ibPropertyUString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString)
	{
		SetValue(value);
	}

};

//base property for "general" - unique name or empty value
class FRMCLIENT_API ibPropertyUEString : public ibPropertyUString {
public:

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyUString(cat, name, value)
	{
		SetValue(value);
	}

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyUString(cat, name, label, value)
	{
		SetValue(value);
	}

	ibPropertyUEString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyUString(cat, name, label, helpString, value)
	{
		SetValue(value);
	}

};

//base property for "caption" - for translate
// ⭐ IT HOLDS AN ibTranslateString, the way the number property holds an ibNumber — so it stands on
// ibPropertySerializable and not on the string base. A string property stores a string and its setter replaces
// it, which is exactly how every other language used to be lost.
class FRMCLIENT_API ibPropertyTString : public ibPropertySerializable {
public:

	// …held as its node, the raw text (ibPropertySerializable::ReadNodeValue).
	ibTranslateString GetValueAsTranslate() const;
	void SetValue(const ibTranslateString& translate);

	// THE ACTIVE SYNONYM — the text, in the language in force. It is the translation converting
	// itself, and it is the one question a label, a tooltip or a page header ever asks.
	wxString GetValueAsTranslateString() const { return GetValueAsTranslate().GetString(); }

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
		const ibTranslateString& value) : ibPropertySerializable(cat, name, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const ibTranslateString& value) : ibPropertySerializable(cat, name, label, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const ibTranslateString& value) : ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(value);
	}

	virtual bool IsEmptyProperty() const { return GetValueAsTranslate().IsEmpty(); }

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

//base property for "text"
class FRMCLIENT_API ibPropertyMString : public ibPropertyStringBase {
public:

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name)
	{
		SetValue(value);
	}

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label)
	{
		SetValue(value);
	}

	ibPropertyMString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString)
	{
		SetValue(value);
	}

};

#endif
