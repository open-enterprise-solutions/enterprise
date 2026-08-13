#ifndef _VALUE_TYPE_H__
#define _VALUE_TYPE_H__

#include "backend/compiler/value.h"
#include "backend/backend_type.h"

class BACKEND_API ibValueType : public ibValue {
	public:

	ibClassID GetOwnerTypeClass() const { return m_clsid; }
	ibTypeDescription GetOwnerTypeDescription() const { return ibTypeDescription(GetOwnerTypeClass()); }

	ibValueType(const ibClassID& clsid = 0);
	ibValueType(const ibValue& cObject);
	ibValueType(const ibValueType& typeObject);

	ibValueType(const wxString& typeName);

	virtual bool IsEmpty() const { return false; }

	virtual bool CompareValueEQ(const ibValue& cParam) const {
		const ibValueType* rValue = CastValue<ibValueType>(cParam);
		wxASSERT(rValue);
		return m_clsid == rValue->m_clsid;
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const {
		const ibValueType* rValue = CastValue<ibValueType>(cParam);
		wxASSERT(rValue);
		return m_clsid != rValue->m_clsid;
	}

	virtual wxString GetString() const;

private:
	ibClassID m_clsid;
};

class BACKEND_API ibValueQualifierNumber : public ibValue {
	public:
	ibQualifierNumber m_qNumber;
public:

	ibValueQualifierNumber() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	ibValueQualifierNumber(unsigned char precision, unsigned char scale) : ibValue(ibValueTypes::TYPE_VALUE, true),
		m_qNumber(precision, scale)
	{
	}

	operator ibQualifierNumber() const { return m_qNumber; }
};

class BACKEND_API ibValueQualifierDate : public ibValue {
	public:
	ibQualifierDate m_qDate;
public:

	ibValueQualifierDate() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	ibValueQualifierDate(ibDateFractions dateTime) : ibValue(ibValueTypes::TYPE_VALUE, true),
		m_qDate(dateTime)
	{
	}

	operator ibQualifierDate() const { return m_qDate; }
};

class BACKEND_API ibValueQualifierString : public ibValue {
	public:
	ibQualifierString m_qString;
public:

	ibValueQualifierString() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	ibValueQualifierString(unsigned short length) : ibValue(ibValueTypes::TYPE_VALUE, true),
		m_qString(length)
	{
	}

	operator ibQualifierString() const { return m_qString; }
};

void ibValueTypeDescription_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// The clsid the column layout gates its blob slot on — declared next to the value so the tier does
// not have to spell the string (the same arrangement as g_valueScheduleCLSID in valueJob.h). A
// second place that knows the spelling is a second place to get it wrong.
constexpr ibClassID g_valueTypeDescriptionCLSID = value_to_clsid("VL_TYPED");

class BACKEND_API ibValueTypeDescription : public ibValueStaticMembers<&ibValueTypeDescription_BindNames> {
public:
	ibTypeDescription m_typeDesc;
public:

	// DoGetPMethods (protected) + Shared<&ibValueTypeDescription_BindNames> come from the base.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

public:

	static ibValue AdjustValue(const ibTypeDescription& typeDescription,
		const class ibMetaData* metaData = nullptr);

	static ibValue AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
		const class ibMetaData* metaData = nullptr);

	ibValueTypeDescription();

	ibValueTypeDescription(class ibValueType* valueType);
	ibValueTypeDescription(class ibValueType* valueType,
		ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString);

	ibValueTypeDescription(const ibTypeDescription& typeDescription);

	ibValueTypeDescription(const std::vector<ibClassID>& array);
	ibValueTypeDescription(const std::vector<ibClassID>& array,
		ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString);

	virtual ~ibValueTypeDescription();

	// AN EMPTY DESCRIPTION IS A LEGITIMATE START, not a failed construction. Refusing here meant the
	// value could only ever be born from arguments (`New TypeDescription(...)` in a script) — so a
	// FIELD holding one had nothing to create when the user first clicked it, and the editor died
	// with "Error initializing object" before any type could be chosen.
	//
	// Empty is still not a valid SAVE: the characteristic's Type attribute is fill-checked, and the
	// write refuses. Creation and completeness are different questions, and only the second one is
	// the attribute's.
	virtual bool Init() { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray);

public:

	operator ibTypeDescription() const { return GetTypeDesc(); }

	const std::vector<ibClassID>& GetClsidList() { return m_typeDesc.GetClsidList(); }
	const ibTypeDescription& GetTypeDesc() const { return m_typeDesc; }

	// WHAT IT IS MADE OF — the types it admits, by name, comma-separated. A description shown as
	// "TypeDescription" tells the reader the CLASS of the cell, which they can see from the field
	// anyway; what they need is the content, and for a composite that is the whole point of it
	// being composite. Empty stays empty — a field that has not been given a type says nothing
	// rather than inventing a word for it.
	virtual wxString GetString() const override;

	// EMPTY MEANS "NAMES NO TYPE". The base answers `false` for every value object — an object
	// exists, therefore it is not empty — which is right for a schedule (its defaults mean
	// something) and wrong here: a description with no types admits nothing and describes nothing.
	//
	// Without this the fill check could not see the difference between a characteristic whose type
	// was chosen and one whose editor was merely opened, so a required Type was satisfied by the
	// bare act of clicking the field.
	virtual bool IsEmpty() const override { return m_typeDesc.GetClsidCount() == 0; }

public:

	bool ContainType(const ibValue& cType) const;
	ibValue AdjustValue() const;
	ibValue AdjustValue(const ibValue& varValue) const;
	ibValue Types() const;
};

#endif