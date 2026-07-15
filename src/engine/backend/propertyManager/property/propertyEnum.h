#ifndef __PROPERTY_ENUM_H__
#define __PROPERTY_ENUM_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "enum"
class BACKEND_API ibPropertyEnumBase : public ibProperty {
public:

	long GetValueAsInteger() const { return m_propValue; }
	void SetValue(const long& i) { m_propValue = i; }

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name,
		const wxVariant& value) : ibProperty(cat, name, value)
	{
	}

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxVariant& value) : ibProperty(cat, name, label, value)
	{
	}

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxVariant& value) : ibProperty(cat, name, label, helpString, value)
	{
	}

	//get property for grid 
	virtual wxObject* GetPGProperty() const final {
		if (ms_propertyEnum != nullptr)
			return ms_propertyEnum(m_propLabel, m_propName, GetEnumList(), GetValueAsInteger());
		return nullptr;
	}

	// The choices this enum offers. Public because the FRONT builds the editor now and has
	// to read them (it was protected while the property built its own wxPGProperty and only
	// needed them inside GetPGProperty).
	virtual ibPropertyChoiceList GetEnumList() const = 0;

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) = 0;
	virtual bool GetDataValue(ibValue& pvarPropVal) const = 0;

	//load & save object in control

	// readable node value (typed Number = the enum's integer)
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyEnum)(const wxString&, const wxString&, const ibPropertyChoiceList&, const int&);
};

#include "backend/compiler/enumUnit.h"

template <typename valEnumProp>
class ibPropertyEnum : public ibPropertyEnumBase {
	using valueEnumType = typename valEnumProp::valEnumType;
public:

	valueEnumType GetValueAsEnum() const { return static_cast<valueEnumType>(ibPropertyEnumBase::GetValueAsInteger()); }
	int GetValueAsInteger() const { return ibPropertyEnumBase::GetValueAsInteger(); }
	void SetValue(const valueEnumType& e) { ibPropertyEnumBase::SetValue(static_cast<int>(e)); }
	void SetValue(const int& i) { ibPropertyEnumBase::SetValue(i); }

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name, static_cast<long>(value))
	{
	}

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name, label, static_cast<long>(value))
	{
	}

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name, label, helpString, static_cast<long>(value))
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) {
		SetValue(varPropVal.ConvertToEnumValue<valueEnumType>());
		return true;
	};

	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		ibValue enumVariant = ibPropertyEnum::GetValueAsInteger();
		ibValue* ppParams[] = { &enumVariant, nullptr };
		if (m_enumCreator->Init(ppParams, 1)) {
			pvarPropVal = m_enumCreator->GetEnumVariantValue();
			return true;
		}
		pvarPropVal = m_enumCreator;
		return true;
	};

protected:
	virtual ibPropertyChoiceList GetEnumList() const {
		ibPropertyChoiceList list;
		for (unsigned int idx = 0; idx < m_enumCreator->GetEnumCount(); idx++) {
			list.Add(m_enumCreator->GetEnumDesc(idx), m_enumCreator->GetEnumValue(idx));
		}
		return list;
	}
private:
	ibValuePtr<valEnumProp> m_enumCreator = ibValuePtr<valEnumProp>(ibValue::CreateAndConvertObjectRef<valEnumProp>());
};

#endif