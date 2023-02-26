#ifndef _VALUE_TYPE_H__
#define _VALUE_TYPE_H__

#include "core/compiler/value.h"
#include "core/common/typeInfo.h"

class CValueType : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueType);
public:

	CLASS_ID GetOwnerTypeClass() const {
		return m_clsid;
	}

	typeDescription_t GetOwnerTypeDescription() const {
		return typeDescription_t(GetOwnerTypeClass());
	}

	CValueType(const CLASS_ID& clsid = 0);
	CValueType(const CValue& cObject);
	CValueType(const CValueType& typeObject);

	CValueType(const wxString& typeName);

	virtual inline bool IsEmpty() const override {
		return false;
	}

	virtual bool CompareValueEQ(const CValue& cParam) const {
		const CValueType* rValue = value_cast<CValueType>(cParam);
		wxASSERT(rValue);
		return m_clsid == rValue->m_clsid;
	}

	//operator '!='
	virtual bool CompareValueNE(const CValue& cParam) const {
		const CValueType* rValue = value_cast<CValueType>(cParam);
		wxASSERT(rValue);
		return m_clsid != rValue->m_clsid;
	}

	virtual wxString GetTypeString() const {
		return wxT("type");
	}
	virtual wxString GetString() const;

private:
	CLASS_ID m_clsid;
};

class CValueQualifierNumber : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierNumber);
public:
	qualifierNumber_t m_qNumber;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierNumber");
	}

	virtual wxString GetString() const {
		return wxT("qualifierNumber");
	}

	CValueQualifierNumber() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierNumber(unsigned char precision, unsigned char scale) : CValue(eValueTypes::TYPE_VALUE, true),
		m_qNumber(precision, scale)
	{
	}

	operator qualifierNumber_t() const {
		return m_qNumber;
	}
};

class CValueQualifierDate : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierDate);
public:
	qualifierDate_t m_qDate;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierDate");
	}

	virtual wxString GetString() const {
		return wxT("qualifierDate");
	}

	CValueQualifierDate() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierDate(eDateFractions dateTime) : CValue(eValueTypes::TYPE_VALUE, true),
		m_qDate(dateTime)
	{
	}

	operator qualifierDate_t() const {
		return m_qDate;
	}
};

class CValueQualifierString : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierString);
public:
	qualifierString_t m_qString;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierString");
	}

	virtual wxString GetString() const {
		return wxT("qualifierString");
	}

	CValueQualifierString() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierString(unsigned short length) : CValue(eValueTypes::TYPE_VALUE, true),
		m_qString(length)
	{
	}

	operator qualifierString_t() const {
		return m_qString;
	}
};

class CValueTypeDescription : public CValue {
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(CValueTypeDescription);
private:
	CMethodHelper* m_methodHelper;
public:
	typeDescription_t m_typeDescription;
public:

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

public:

	static CValue AdjustValue(const typeDescription_t& typeDescription);
	static CValue AdjustValue(const typeDescription_t& typeDescription, const CValue& varValue);

	CValueTypeDescription();

	CValueTypeDescription(class CValueType* valueType);
	CValueTypeDescription(class CValueType* valueType,
		CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString);

	CValueTypeDescription(const typeDescription_t& typeDescription);

	CValueTypeDescription(const std::set<CLASS_ID>& clsid);
	CValueTypeDescription(const std::set<CLASS_ID>& clsid,
		CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString);

	virtual ~CValueTypeDescription();

	virtual bool Init() { return false; }
	virtual bool Init(CValue** paParams, const long lSizeArray);

	virtual wxString GetTypeString() const { return wxT("typeDescription"); }
	virtual wxString GetString() const {
		return wxT("typeDescription");
	}

public:

	operator typeDescription_t() const {
		return GetTypeDescription();
	}

	std::set<CLASS_ID> GetClsids() {
		return m_typeDescription.m_clsids;
	}

	typeDescription_t GetTypeDescription() const {
		return m_typeDescription;
	}

public:

	bool ContainType(const CValue& cType) const;
	CValue AdjustValue() const;
	CValue AdjustValue(const CValue& varValue) const;
	CValue Types() const;
};

#endif