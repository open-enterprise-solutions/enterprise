#ifndef __PROPERTY_ENUM_H__
#define __PROPERTY_ENUM_H__

#include "frmclient/backend/propertyManager/property/propertySerializable.h"

//base property for "enum"
class FRMCLIENT_API ibPropertyEnumBase : public ibPropertySerializable {
public:

	// …held as its node, the chosen one's id (ibPropertySerializable::ReadNodeValue) — set by the enumeration it is.
	long GetValueAsInteger() const;
	void SetValue(const long& i);

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name) : ibPropertySerializable(cat, name, wxNullVariant)
	{
	}

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibPropertySerializable(cat, name, label, wxNullVariant)
	{
	}

	ibPropertyEnumBase(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
	}

	// The choices this enum offers. Public because the FRONT builds the editor now and has
	// to read them (it was protected while the property built its own wxPGProperty and only
	// needed them inside GetPGProperty).
	virtual ibPropertyChoiceList GetEnumList() const = 0;

	// THE SAME QUESTION THE FAMILY ASKS — an enumeration's answer is fixed by its type, which is why
	// it had a name of its own and why nothing generic could reach it. One word for it now; the
	// type-fixed list stays where it belongs, above.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override {
		list = GetEnumList();
		return ibPropertyChoiceMode::Single;
	}

	//load & save object in control

	// readable node value (typed Number = the enum's integer)
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#include "frmclient/backend/compiler/enumUnit.h"

template <typename valEnumProp>
class ibPropertyEnum : public ibPropertyEnumBase {
	using valueEnumType = typename valEnumProp::valEnumType;
public:

	valueEnumType GetValueAsEnum() const { return static_cast<valueEnumType>(ibPropertyEnumBase::GetValueAsInteger()); }
	int GetValueAsInteger() const { return ibPropertyEnumBase::GetValueAsInteger(); }
	void SetValue(const valueEnumType& e) { ibPropertyEnumBase::SetValue(static_cast<int>(e)); }
	void SetValue(const int& i) { ibPropertyEnumBase::SetValue(i); }

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name)
	{
		SetValue(value);
	}

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name, label)
	{
		SetValue(value);
	}

	ibPropertyEnum(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const valueEnumType& value) : ibPropertyEnumBase(cat, name, label, helpString)
	{
		SetValue(value);
	}

protected:
	virtual ibPropertyChoiceList GetEnumList() const {
		ibPropertyChoiceList list;
		for (unsigned int idx = 0; idx < m_enumCreator->GetEnumCount(); idx++) {
			list.Add(m_enumCreator->GetEnumDesc(idx), m_enumCreator->GetEnumValue(idx));
		}
		return list;
	}
private:
	ibValuePtr<valEnumProp> m_enumCreator = ibValue::CreateObject<valEnumProp>();
};

#endif