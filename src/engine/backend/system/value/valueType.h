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

class BACKEND_API ibValueTypeDescription : public ibValue {
private:
	ibValueMethodHelper* m_methodHelper;
public:
	ibTypeDescription m_typeDesc;
public:

	// these methods need to be overridden in your aggregate objects:
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

public:

	static ibValue AdjustValue(const ibTypeDescription& typeDescription,
		class ibMetaData* metaData = nullptr);

	static ibValue AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
		class ibMetaData* metaData = nullptr);

	ibValueTypeDescription();

	ibValueTypeDescription(class ibValueType* valueType);
	ibValueTypeDescription(class ibValueType* valueType,
		ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString);

	ibValueTypeDescription(const ibTypeDescription& typeDescription);

	ibValueTypeDescription(const std::vector<ibClassID>& array);
	ibValueTypeDescription(const std::vector<ibClassID>& array,
		ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString);

	virtual ~ibValueTypeDescription();

	virtual bool Init() { return false; }
	virtual bool Init(ibValue** paParams, const long lSizeArray);

public:

	operator ibTypeDescription() const { return GetTypeDesc(); }

	const std::vector<ibClassID>& GetClsidList() { return m_typeDesc.GetClsidList(); }
	const ibTypeDescription& GetTypeDesc() const { return m_typeDesc; }

public:

	bool ContainType(const ibValue& cType) const;
	ibValue AdjustValue() const;
	ibValue AdjustValue(const ibValue& varValue) const;
	ibValue Types() const;
};

#endif