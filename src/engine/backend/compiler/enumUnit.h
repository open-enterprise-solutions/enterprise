#ifndef __ENUM_UNIT_H__
#define __ENUM_UNIT_H__

#include "value.h"

class BACKEND_API ibValueEnumerationWrapper : public ibValue {
	ibValueMethodHelper* m_methodHelper;
public:

	ibValueEnumerationWrapper(bool createInstance = false);
	virtual ~ibValueEnumerationWrapper();

	virtual ibValueMethodHelper* GetPMethods() const { return m_methodHelper; }
	virtual void PrepareNames() const;

	virtual ibValue* GetEnumVariantValue() const = 0;
	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:
	std::vector<wxString> m_listEnumStr;
};

//***************************************************************************************************
//*                                 Current variant from ibValueEnumerationBase                     *
//***************************************************************************************************

template <typename valT>
class ibValueEnumerationVariantBase : public ibValue {
public:

	ibValueEnumerationVariantBase() : ibValue(ibValueTypes::TYPE_ENUM, true) {}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(const valT& v) = 0;
};

//***************************************************************************************************
//*                                       Base collection variant                                   *
//***************************************************************************************************

template <typename valT>
class ibValueEnumerationBase : public ibValueEnumerationWrapper {
public:

	ibValueEnumerationBase(bool createInstance = false) :
		ibValueEnumerationWrapper(createInstance)
	{
	}

	virtual bool Init() { return true; }

	virtual bool Init(ibValue** paParams, const long lSizeArray) {
		if (lSizeArray < 1)
			return false;
		SetEnumValue(
			static_cast<valT>(paParams[0]->GetInteger())
		);
		return true;
	}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(const valT& v) = 0;
};

//default base class for all enumerations
template <typename valT>
class ibValueEnumeration : public ibValueEnumerationBase<valT> {
	std::map<valT, wxString> m_listEnumData, m_listEnumDesc;
protected:

	template <typename valType>
	class ibValueEnumerationVariant : public ibValueEnumerationVariantBase<valType> {
	public:

		ibValueEnumerationVariant(const valType& v, const ibClassID& clsid) : ibValueEnumerationVariantBase<valType>(), m_clsid(clsid), m_value(v) {}

		void CreateEnumeration(
			const wxString& name, const wxString& descr,
			const valType& v) {
			m_name = name;
			m_description = descr;
			m_value = v;
		}

		virtual valT GetEnumValue() const override { return m_value; }
		virtual void SetEnumValue(const valT& v) override { m_value = v; }

		virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const override {
			ibValuePtr<ibValueEnumeration<valType>> enumOwner(ibValue::CreateAndConvertObjectRef<ibValueEnumeration<valType>>(m_clsid));
			for (auto& e : enumOwner->m_listEnumData) {
				if (e.second.Contains(findData)) {
					ibValueEnumerationVariant<valType>* enumValue = new ibValueEnumerationVariant<valType>(e.first, m_clsid);
					if (enumValue != nullptr) {
						enumValue->CreateEnumeration(
							enumOwner->GetEnumName(e.first),
							enumOwner->GetEnumDescription(e.first),
							e.first
						);
						listValue.push_back(enumValue);
					}
				}
			}
			std::sort(listValue.begin(), listValue.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
			return listValue.size() > 0;
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value == compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value == compareEnumerationOwner->GetEnumValue();
			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value != compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value != compareEnumerationOwner->GetEnumValue();
			return true;
		}

		//get type id
		virtual ibClassID GetClassType() const override { return m_clsid; }

		//check is empty
		virtual bool IsEmpty() const override { return false; }

		//type info
		virtual wxString GetClassName() const override { return ibValue::GetNameObjectFromID(m_clsid); }

		//type conversion
		virtual wxString GetString() const override { return m_name; }
		virtual ibNumber GetNumber() const override { return m_value; }

	private:
		wxString m_name;
		wxString m_description;
		ibClassID m_clsid;
		valType m_value;
	};

	virtual wxString GetEnumName(const valT& v) const { return m_listEnumData.at(v); }
	virtual wxString GetEnumDescription(const valT& v) const { return m_listEnumDesc.at(v); }

private:

	inline void CreateEnumeration(const valT& v) {
		wxASSERT(m_listEnumData.find(v) != m_listEnumData.end());
		if (m_value != nullptr) {
			m_value->CreateEnumeration(
				GetEnumName(v),
				GetEnumDescription(v),
				v
			);
		}
	}

public:

	using valEnumType = valT;

	inline void AddEnumeration(const valT& v, const wxString& name, const wxString& descr = wxEmptyString) {
		wxASSERT(m_listEnumData.find(v) == m_listEnumData.end());
		m_listEnumData.insert_or_assign(v, name);
		m_listEnumDesc.insert_or_assign(v, descr.IsEmpty() ? name : descr);

		this->m_listEnumStr.push_back(name);
	}

	ibValueEnumeration() : ibValueEnumerationBase<valT>(true), m_value(nullptr) { InitializeEnumeration(); }
	virtual ~ibValueEnumeration() {}

	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override {
		if (lSizeArray < 1)
			return false;
		const valT& defValue = static_cast<valT>(paParams[0]->GetInteger());
		ibValueEnumeration::InitializeEnumeration(defValue);
		return true;
	}

	virtual valT GetDefaultEnumValue() const {
		auto itEnums = m_listEnumData.begin();
		std::advance(itEnums, 1);
		if (itEnums != m_listEnumData.end())
			return itEnums->first;
		return valT();
	};

	virtual valT GetEnumValue() const override {
		if (m_value != nullptr) {
			return m_value->GetEnumValue();
		}
		return valT();
	}

	virtual void SetEnumValue(const valT& v) override {
		if (m_value != nullptr) {
			m_value->CreateEnumeration(
				GetEnumName(v),
				GetEnumDescription(v),
				v
			);
		}
	}

	//create enumeration 
	virtual void CreateEnumeration() = 0;

	ibValueEnumerationVariant<valT>* CreateEnumVariantValue(const valT& v) const {

		ibValueEnumerationVariant<valT>* enumValue = new ibValueEnumerationVariant<valT>(v, ibValue::GetClassType());
		if (enumValue != nullptr) {
			enumValue->CreateEnumeration(
				GetEnumName(v),
				GetEnumDescription(v),
				v
			);
			return enumValue;
		}

		return nullptr;
	}

	//initialize enumeration 
	void InitializeEnumeration() {
		this->PrepareNames();
	}

	void InitializeEnumeration(const valT& v) {
		m_value = CreateEnumVariantValue(v);
		CreateEnumeration(v);
	}

	virtual ibValue* GetEnumVariantValue() const override { return m_value; }

	wxString GetEnumName(unsigned int idx) const {
		if (idx > m_listEnumData.size())
			return wxEmptyString;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->second;
	}

	wxString GetEnumDesc(unsigned int idx) const {
		if (idx > m_listEnumDesc.size())
			return wxEmptyString;
		auto it = m_listEnumDesc.begin();
		std::advance(it, idx);
		return it->second;
	}

	valT GetEnumValue(unsigned int idx) const {
		if (idx > m_listEnumData.size())
			return (valT)0;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->first;
	}

	unsigned int GetEnumCount() const { return m_listEnumData.size(); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { //attribute value
		auto itEnums = m_listEnumData.begin();
		std::advance(itEnums, lPropNum);
		if (itEnums != m_listEnumData.end()) {
			ibValueEnumerationVariant<valT>* enumValue =
				new ibValueEnumerationVariant<valT>(itEnums->first, ibValue::GetClassType());
			if (enumValue != nullptr) {
				enumValue->CreateEnumeration(
					GetEnumName(itEnums->first),
					GetEnumDescription(itEnums->first),
					itEnums->first
				);
				pvarPropVal = enumValue;
				return true;
			}
			return false;
		}
		return false;
	}

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const override {
		for (auto& e : m_listEnumData) {
			if (e.second.Contains(findData)) {
				ibValueEnumerationVariant<valT>* enumValue = new ibValueEnumerationVariant<valT>(e.first, ibValue::GetClassType());
				if (enumValue != nullptr) {
					enumValue->CreateEnumeration(
						GetEnumName(e.first),
						GetEnumDescription(e.first),
						e.first
					);
					listValue.push_back(enumValue);
				}
			}
		}
		std::sort(listValue.begin(), listValue.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		return listValue.size() > 0;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueEQ(cParam);
		}
		return ibValue::CompareValueEQ(cParam);
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueNE(cParam);
		}
		return ibValue::CompareValueNE(cParam);
	}

	//check is empty
	virtual bool IsEmpty() const override { return false; }

	//type info
	virtual wxString GetClassName() const final {
		return m_value ? m_value->GetClassName() :
			ibValue::GetClassName();
	};

	//type conversion
	virtual wxString GetString() const final {
		return m_value ? m_value->GetString() :
			wxString(wxEmptyString);
	}

	virtual ibNumber GetNumber() const final {
		return m_value ? m_value->GetNumber() :
			wxNOT_FOUND;
	}

protected:

	ibValuePtr<ibValueEnumerationVariant<valT>> m_value; //current enum value
};

#endif