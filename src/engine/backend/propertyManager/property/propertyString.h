#ifndef __PROPERTY_STRING_H__
#define __PROPERTY_STRING_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_localization.h"

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
class BACKEND_API ibPropertyTString : public ibPropertyStringBase {
public:

	wxString GetValueAsTranslateString() const {
		static thread_local wxString result;
		GetValueAsTranslateString(result);
		return std::move(result);
	}

	bool GetValueAsTranslateString(wxString& result) const {
		if (GetValueAsString(result))
			return ibBackendLocalization::GetTranslateGetRawLocText(result, result);
		return false;
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name,
		const wxString& value) : ibPropertyStringBase(cat, name, ibBackendLocalization::CreateLocalizationRawLocText(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxString& value) : ibPropertyStringBase(cat, name, label, ibBackendLocalization::CreateLocalizationRawLocText(value))
	{
	}

	ibPropertyTString(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxString& value) : ibPropertyStringBase(cat, name, label, helpString, ibBackendLocalization::CreateLocalizationRawLocText(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsTranslateString().IsEmpty(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;
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