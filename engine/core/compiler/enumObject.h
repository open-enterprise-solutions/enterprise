#ifndef ENUM_OBJECT_H__
#define ENUM_OBJECT_H__

#include "enum.h"

//default base class for all enumerations
template <typename valT>
class IEnumeration : public IEnumerationValue<valT>
{
	std::map<valT, wxString> m_aEnumValues;

protected:

	template <typename valType>
	class CEnumerationVariant : public IEnumerationVariant<valType>
	{
		wxString m_type;
		wxString m_name;
		wxString m_description;
		CLASS_ID m_clsid;

		valType m_value;

	public:

		CEnumerationVariant(valType value, const CLASS_ID& clsid) : IEnumerationVariant(),
			m_value(value), m_clsid(clsid)
		{
		}

		void CreateEnumeration(const wxString& typeName,
			const wxString& name, const wxString& descr,
			valType value) {
			m_type = typeName;
			m_name = name;
			m_description = descr;
			m_value = value;
		}

		virtual valT GetEnumValue() const override {
			return m_value;
		}

		virtual void SetEnumValue(valT val) override {
			m_value = val;
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override
		{
			CEnumerationVariant<valType>* compareEnumeration = dynamic_cast<CEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value == compareEnumeration->m_value;
			IEnumeration<valType>* compareEnumerationOwner = dynamic_cast<IEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value == compareEnumerationOwner->GetEnumValue();
			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override
		{
			CEnumerationVariant<valType>* compareEnumeration = dynamic_cast<CEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value != compareEnumeration->m_value;
			IEnumeration<valType>* compareEnumerationOwner = dynamic_cast<IEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value != compareEnumerationOwner->GetEnumValue();
			return true;
		}

		//get type id
		virtual CLASS_ID GetClassType() const override {
			return m_clsid;
		}

		//check is empty
		virtual inline bool IsEmpty() const override {
			return false;
		}

		//type info
		virtual wxString GetTypeString() const override {
			return m_type;
		}

		//type conversion
		virtual wxString GetString() const override {
			return m_name;
		}

		virtual number_t GetNumber() const override {
			return m_value;
		}
	};

	virtual wxString GetEnumName(valT val) const {
		return m_aEnumValues.at(val);
	}

	virtual wxString GetEnumDescription(valT val) const {
		return wxEmptyString;
	}

	friend class PropertyOption;

private:

	inline void CreateEnumeration(valT val)
	{
		wxASSERT(m_aEnumValues.find(val) != m_aEnumValues.end());

		if (m_value != NULL) {
			m_value->CreateEnumeration(
				GetTypeString(),
				GetEnumName(val),
				GetEnumDescription(val),
				val
			);
		}
	}

public:

	inline void AddEnumeration(valT val, const wxString& name, const wxString& descr = wxEmptyString)
	{
		wxASSERT(m_aEnumValues.find(val) == m_aEnumValues.end());
		m_aEnumValues.insert_or_assign(val, name);
		m_aEnumsString.push_back(name);
	}

	IEnumeration() : IEnumerationValue(true), m_value(NULL) {}
	IEnumeration(valT defValue) : IEnumerationValue(), m_value(NULL) {}

	virtual ~IEnumeration() {
		if (m_value != NULL)
			m_value->DecrRef();
	}

	virtual bool Init() {
		return true;
	}

	virtual bool Init(CValue** aParams) {
		valT defValue = static_cast<valT>(aParams[0]->ToInt());
		IEnumeration::InitializeEnumeration(defValue);
		return true;
	}

	virtual valT GetDefaultEnumValue() const
	{
		auto itEnums = m_aEnumValues.begin();
		std::advance(itEnums, 1);
		if (itEnums != m_aEnumValues.end()) {
			return itEnums->first;
		}
		return valT();
	};

	virtual valT GetEnumValue() const override {
		if (m_value != NULL) {
			return m_value->GetEnumValue();
		}
		return valT();
	}

	virtual void SetEnumValue(valT val) override {
		if (m_value != NULL) {
			m_value->CreateEnumeration(
				GetTypeString(),
				GetEnumName(val),
				GetEnumDescription(val),
				val
			);
		}
	}

	//initialize enumeration 
	virtual void InitializeEnumeration() {
		PrepareNames();
	}

	virtual void InitializeEnumeration(valT defValue) {
		if (m_value != NULL)
			m_value->DecrRef();
		m_value = new CEnumerationVariant(defValue, ITypeValue::GetClassType());
		m_value->IncrRef();
		CreateEnumeration(defValue);
	}

	virtual CValue* GetEnumVariantValue() const
	{
		return m_value;
	}

	virtual CValue GetAttribute(attributeArg_t& aParams) override //значение атрибута
	{
		auto itEnums = m_aEnumValues.begin();
		std::advance(itEnums, aParams.GetIndex());

		if (itEnums != m_aEnumValues.end()) {
			CEnumerationVariant<valT>* enumValue =
				new CEnumerationVariant<valT>(itEnums->first, ITypeValue::GetClassType());
			enumValue->CreateEnumeration(
				GetTypeString(),
				GetEnumName(itEnums->first),
				GetEnumDescription(itEnums->first),
				itEnums->first
			);
			return enumValue;
		}

		return CValue();
	}

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue& cParam) const override
	{
		if (m_value != NULL) {
			return m_value->CompareValueEQ(cParam);
		}
		return CValue::CompareValueEQ(cParam);
	}

	//operator '!='
	virtual inline bool CompareValueNE(const CValue& cParam) const override
	{
		if (m_value != NULL) {
			return m_value->CompareValueNE(cParam);
		}
		return CValue::CompareValueNE(cParam);
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false;
	}

	//type info
	virtual wxString GetTypeString() const = 0;

	//type conversion
	virtual wxString GetString() const override {
		return m_value ? m_value->GetString() :
			GetTypeString();
	}

	virtual number_t GetNumber() const override {
		return m_value ? m_value->GetNumber() :
			wxNOT_FOUND;
	}

protected:

	CEnumerationVariant<valT>* m_value; //current enum value
};

#endif