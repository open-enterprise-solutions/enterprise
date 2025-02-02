#ifndef _TYPE_INFO_H__
#define _TYPE_INFO_H__

enum eDateFractions {
	eDateFractions_Date = 0,
	eDateFractions_DateTime,
	eDateFractions_Time
};

enum eAllowedLength {
	eAllowedLength_Variable,
	eAllowedLength_Fixed
};

#define DEF_START_ITEM_PER_COUNT 60

struct qualifierNumber_t {
	bool m_nonNegative;
	unsigned char m_precision;
	unsigned char m_scale;
	qualifierNumber_t(unsigned char precision = 10, char scale = 0, bool nonNegative = false) :
		m_precision(precision), m_scale(scale), m_nonNegative(nonNegative) {}
};

struct qualifierDate_t {
	eDateFractions m_dateTime;
	qualifierDate_t(eDateFractions dateTime = eDateFractions::eDateFractions_DateTime) : m_dateTime(dateTime) {
	}
};

struct qualifierString_t {
	unsigned short m_length;
	eAllowedLength m_allowedLength;
	qualifierString_t(unsigned short length = 10, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) :
		m_length(length), m_allowedLength(allowedLength) {}
};

////////////////////////////////////////////////////////////////////////////////
class BACKEND_API IMetaData;
////////////////////////////////////////////////////////////////////////////////

struct typeDescription_t {
	std::set<class_identifier_t> m_clsids;
	struct typeData_t {
		qualifierNumber_t m_number;
		qualifierDate_t	  m_date;
		qualifierString_t m_string;
	public:
		typeData_t() :
			m_number(10, 0), m_date(eDateFractions::eDateFractions_Date), m_string(10) {} //empty 
		typeData_t(unsigned char precision, unsigned char scale, bool nonnegative = false) :
			m_number(precision, scale, nonnegative), m_date(eDateFractions::eDateFractions_Date), m_string(10) {}
		typeData_t(eDateFractions dateTime) :
			m_number(10, 0), m_date(dateTime), m_string(10) {}
		typeData_t(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) :
			m_number(10, 0), m_date(eDateFractions::eDateFractions_Date), m_string(length, allowedLength) {}
		typeData_t(const qualifierNumber_t& qNumber, const qualifierDate_t& qDate, const qualifierString_t& qString) :
			m_number(qNumber), m_date(qDate), m_string(qString) {}

		//get special data number 
		unsigned char GetPrecision() const {
			return m_number.m_precision;
		}

		unsigned char GetScale() const {
			return m_number.m_scale;
		}

		bool IsNonNegative() const {
			return m_number.m_nonNegative;
		}

		//get special data date  
		eDateFractions GetDateFraction() const {
			return m_date.m_dateTime;
		}

		//get special data string  
		unsigned short GetLength() const {
			return m_string.m_length;
		}

		eAllowedLength GetAllowedLength() const {
			return m_string.m_allowedLength;
		}

		void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false) {
			m_number.m_precision = precision;
			m_number.m_scale = scale;
			m_number.m_nonNegative = nonNegative;
		}

		void SetDate(eDateFractions dateTime) {
			m_date.m_dateTime = dateTime;
		}

		void SetString(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) {
			m_string.m_length = length;
			m_string.m_allowedLength = allowedLength;
		}
	};
	typeData_t m_typeData;
public:

	bool IsOk() const {
		return m_clsids.size() > 0;
	}

	class_identifier_t GetFirstClsid() const {
		if (m_clsids.size() == 0)
			return 0;
		auto it = m_clsids.begin();
		std::advance(it, 0);
		return *it;
	}

	unsigned int GetClsidCount() const {
		return m_clsids.size();
	}

	std::set<class_identifier_t> GetClsids() const {
		return m_clsids;
	}

	typeDescription_t() {}
	typeDescription_t(const class_identifier_t& clsid) : m_clsids({ clsid }) {}
	typeDescription_t(const class_identifier_t& clsid, const typeData_t& descr) : m_clsids({ clsid }), m_typeData(descr) {}
	typeDescription_t(const class_identifier_t& clsid, const qualifierNumber_t& qNumber, const qualifierDate_t& qDate, const qualifierString_t& qString) : m_clsids({ clsid }), m_typeData(qNumber, qDate, qString) {}
	typeDescription_t(const std::set<class_identifier_t>& clsids) : m_clsids(clsids) {}
	typeDescription_t(const std::set<class_identifier_t>& clsids, const typeData_t& descr) : m_clsids(clsids), m_typeData(descr) {}
	typeDescription_t(const std::set<class_identifier_t>& clsids, const qualifierNumber_t& qNumber, const qualifierDate_t& qDate, const qualifierString_t& qString) : m_clsids(clsids), m_typeData(qNumber, qDate, qString) {}

	bool operator == (const typeDescription_t& rhs) const {
		if (m_clsids == rhs.m_clsids) {
			for (auto clsid : m_clsids) {
				eValueTypes valType = CValue::GetVTByID(clsid);
				if (valType == eValueTypes::TYPE_NUMBER) {
					bool result = m_typeData.m_number.m_precision == rhs.m_typeData.m_number.m_precision &&
						m_typeData.m_number.m_scale == rhs.m_typeData.m_number.m_scale &&
						m_typeData.m_number.m_nonNegative == rhs.m_typeData.m_number.m_nonNegative;
					if (!result)
						return false;
				}
				else if (valType == eValueTypes::TYPE_DATE) {
					bool result = m_typeData.m_date.m_dateTime == rhs.m_typeData.m_date.m_dateTime;
					if (!result)
						return false;
				}
				else if (valType == eValueTypes::TYPE_STRING) {
					bool result = m_typeData.m_string.m_length == rhs.m_typeData.m_string.m_length &&
						m_typeData.m_string.m_allowedLength == rhs.m_typeData.m_string.m_allowedLength;
					if (!result)
						return false;
				}
			}
			return true;
		}

		return false;
	}

	bool operator != (const typeDescription_t& rhs) const {
		if (m_clsids == rhs.m_clsids) {
			for (auto clsid : m_clsids) {
				eValueTypes valType = CValue::GetVTByID(clsid);
				if (valType == eValueTypes::TYPE_NUMBER) {
					bool result = m_typeData.m_number.m_precision != rhs.m_typeData.m_number.m_precision ||
						m_typeData.m_number.m_scale != rhs.m_typeData.m_number.m_scale ||
						m_typeData.m_number.m_nonNegative != rhs.m_typeData.m_number.m_nonNegative;
					if (result)
						return true;

				}
				else if (valType == eValueTypes::TYPE_DATE) {
					bool result = m_typeData.m_date.m_dateTime != rhs.m_typeData.m_date.m_dateTime;
					if (result)
						return true;
				}
				else if (valType == eValueTypes::TYPE_STRING) {
					bool result = m_typeData.m_string.m_length != rhs.m_typeData.m_string.m_length ||
						m_typeData.m_string.m_allowedLength != rhs.m_typeData.m_string.m_allowedLength;
					if (result)
						return true;
				}
			}
			return false;
		}
		return true;
	}
};

////////////////////////////////////////////////////////////////////////////////

class BACKEND_API ITypeWrapper {
	typeDescription_t m_typeDescription;
public:

	virtual class_identifier_t GetFirstClsid() const {
		if (m_typeDescription.m_clsids.size() == 0)
			return 0;
		auto it = m_typeDescription.m_clsids.begin();
		std::advance(it, 0);
		return *it;
	}

	virtual std::set<class_identifier_t> GetClsids() const {
		return m_typeDescription.m_clsids;
	}

	virtual bool ContainType(const eValueTypes& valType) const {
		if (valType == eValueTypes::TYPE_ENUM) {
			for (auto clsid : m_typeDescription.m_clsids) {
				if (CValue::IsRegisterCtor(clsid)) {
					IPrimitiveTypeCtor* typeCtor =
						dynamic_cast<IPrimitiveTypeCtor*>(CValue::GetAvailableCtor(clsid));
					if (typeCtor != nullptr) {
						if (typeCtor->GetValueType() == eValueTypes::TYPE_ENUM) {
							return true;
						}
					}
				}
			}
			return false;
		}
		return m_typeDescription.m_clsids.find(CValue::GetIDByVT(valType)) != m_typeDescription.m_clsids.end();
	}

	virtual bool ContainType(const class_identifier_t& clsid) const {
		return m_typeDescription.m_clsids.find(clsid) != m_typeDescription.m_clsids.end();
	}

	virtual class_identifier_t GetByIdx(unsigned int idx) const {
		if (m_typeDescription.m_clsids.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_typeDescription.m_clsids.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	//get special data number 
	unsigned char GetPrecision() const {
		return m_typeDescription.m_typeData.GetPrecision();
	}

	unsigned char GetScale() const {
		return m_typeDescription.m_typeData.GetScale();
	}

	bool IsNonNegative() const {
		return m_typeDescription.m_typeData.IsNonNegative();
	}

	//get special data date  
	eDateFractions GetDateFraction() const {
		return m_typeDescription.m_typeData.GetDateFraction();
	}

	//get special data string  
	unsigned short GetLength() const {
		return m_typeDescription.m_typeData.GetLength();
	}

	eAllowedLength GetAllowedLength() const {
		return m_typeDescription.m_typeData.GetAllowedLength();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void SetDefaultMetatype(const typeDescription_t& typeDesc) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(typeDesc.m_clsids, typeDesc.m_typeData);
	}

	void SetDefaultMetatype(const eValueTypes& valType) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(valType);
	}

	void SetDefaultMetatype(const class_identifier_t& clsid) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(clsid);
	}

	void SetDefaultMetatype(const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(clsid, descr);
	}

	void SetDefaultMetatype(const std::set<class_identifier_t>& clsids) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(clsids);
	}

	void SetDefaultMetatype(const std::set<class_identifier_t>& clsids, const typeDescription_t::typeData_t& descr) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(clsids, descr.m_number, descr.m_date, descr.m_string);
	}

	void SetDefaultMetatype(const std::set<class_identifier_t>& clsids,
		const qualifierNumber_t& qNumber, const qualifierDate_t& qDate, qualifierString_t& qString) {
		m_typeDescription.m_clsids.clear();
		SetMetatype(clsids, qNumber, qDate, qString);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void SetMetatype(const eValueTypes& valType) {
		if (valType == eValueTypes::TYPE_NUMBER) {
			m_typeDescription.m_typeData.SetNumber(10, 0);
		}
		if (valType == eValueTypes::TYPE_DATE) {
			m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (valType == eValueTypes::TYPE_STRING) {
			m_typeDescription.m_typeData.SetString(10);
		}
		m_typeDescription.m_clsids.insert(
			CValue::GetIDByVT(valType)
		);
	}

	void SetMetatype(const class_identifier_t& clsid) {
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_typeDescription.m_typeData.SetNumber(10, 0);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_typeDescription.m_typeData.SetString(10);
		}
		m_typeDescription.m_clsids.insert(clsid);
	}

	void SetMetatype(const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr) {
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_typeDescription.m_typeData.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_typeDescription.m_typeData.SetDate(descr.GetDateFraction());
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_typeDescription.m_typeData.SetString(descr.GetLength(), descr.GetAllowedLength());
		}
		m_typeDescription.m_clsids.insert(clsid);
	}

	void SetMetatype(const std::set<class_identifier_t>& clsids) {
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_typeDescription.m_typeData.SetNumber(10, 0);
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_typeDescription.m_typeData.SetString(10);
		}
		for (auto clsid : clsids) {
			m_typeDescription.m_clsids.insert(clsid);
		}
	}

	void SetMetatype(const std::set<class_identifier_t>& clsids, const typeDescription_t::typeData_t& descr) {
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_typeDescription.m_typeData.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_typeDescription.m_typeData.SetDate(descr.GetDateFraction());
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_typeDescription.m_typeData.SetString(descr.GetLength(), descr.GetAllowedLength());
		}
		for (auto clsid : clsids) {
			m_typeDescription.m_clsids.insert(clsid);
		}
	}

	void SetMetatype(const std::set<class_identifier_t>& clsids,
		const qualifierNumber_t& qNumber, const qualifierDate_t& qDate, const qualifierString_t& qString) {
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_typeDescription.m_typeData.SetNumber(qNumber.m_precision, qNumber.m_scale);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_typeDescription.m_typeData.SetDate(qDate.m_dateTime);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_typeDescription.m_typeData.SetString(qString.m_length);
		}
		for (auto clsid : clsids) {
			m_typeDescription.m_clsids.insert(clsid);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearAllMetatype() {
		m_typeDescription.m_clsids.clear();
		m_typeDescription.m_typeData.SetNumber(10, 0);
		m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
		m_typeDescription.m_typeData.SetString(10);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearMetatype(const class_identifier_t& clsid) {
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_typeDescription.m_typeData.SetNumber(10, 0);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_typeDescription.m_typeData.SetString(10);
		}
		m_typeDescription.m_clsids.erase(clsid);
	}

	void ClearMetatype(const std::set<class_identifier_t>& clsids) {
		for (auto clsid : clsids) {
			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
				m_typeDescription.m_typeData.SetNumber(10, 0);
			}
			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
				m_typeDescription.m_typeData.SetDate(eDateFractions::eDateFractions_DateTime);
			}
			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
				m_typeDescription.m_typeData.SetString(10);
			}
			m_typeDescription.m_clsids.erase(clsid);
		}
	}

	void SetTypeDescription(const typeDescription_t& typeDescription) {
		m_typeDescription = typeDescription;
	}

	void SetTypeData(const typeDescription_t::typeData_t& typeData) {
		m_typeDescription.m_typeData = typeData;
	}

	//get type decription 
	const typeDescription_t& GetTypeDescription() const {
		return m_typeDescription;
	}

	const typeDescription_t::typeData_t& GetTypeData() const {
		return m_typeDescription.m_typeData;
	}

	//////////////////////////////////////////////////

	void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false) {
		auto& it = m_typeDescription.m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER));
		if (it == m_typeDescription.m_clsids.end())
			m_typeDescription.m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER));
		m_typeDescription.m_typeData.SetNumber(precision, scale, nonNegative);
	}

	void SetDate(eDateFractions dateTime) {
		auto& it = m_typeDescription.m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE));
		if (it == m_typeDescription.m_clsids.end())
			m_typeDescription.m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_DATE));
		m_typeDescription.m_typeData.SetDate(dateTime);
	}

	void SetString(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) {
		auto& it = m_typeDescription.m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING));
		if (it == m_typeDescription.m_clsids.end())
			m_typeDescription.m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_STRING));
		m_typeDescription.m_typeData.SetString(length, allowedLength);
	}

	//qualifiers 
	const qualifierNumber_t& GetNumberQualifier() const {
		return m_typeDescription.m_typeData.m_number;
	}

	const qualifierDate_t& GetDateQualifier() const {
		return m_typeDescription.m_typeData.m_date;
	}

	const qualifierString_t& GetStringQualifier() const {
		return m_typeDescription.m_typeData.m_string;
	}

	//////////////////////////////////////////////////

	unsigned int GetClsidCount() const {
		return m_typeDescription.m_clsids.size();
	}

	//////////////////////////////////////////////////

	ITypeWrapper() {}

	ITypeWrapper(const ITypeWrapper& src) :
		m_typeDescription(src.m_typeDescription) {
	}

	ITypeWrapper(const eValueTypes& valType) {
		SetDefaultMetatype(valType);
	}

	ITypeWrapper(const class_identifier_t& clsid) {
		SetDefaultMetatype(clsid);
	}

	ITypeWrapper(const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr) {
		SetDefaultMetatype(clsid, descr);
	}

	ITypeWrapper(const std::set<class_identifier_t>& clsids) {
		SetDefaultMetatype(clsids);
	}

	ITypeWrapper(const std::set<class_identifier_t>& clsids, const typeDescription_t::typeData_t& descr) {
		SetDefaultMetatype(clsids, descr);
	}

	//////////////////////////////////////////////////

	virtual bool LoadTypeData(class CMemoryReader& dataReader);
	virtual bool LoadFromVariant(const wxVariant& variant);
	virtual bool SaveTypeData(class CMemoryWriter& dataWritter) const;
	virtual void SaveToVariant(wxVariant& variant, IMetaData* metaData) const;

	//////////////////////////////////////////////////

	bool EqualsType(const class_identifier_t& clsid,
		const typeDescription_t& rhs) const {
		eValueTypes valType = CValue::GetVTByID(clsid);
		if (valType == eValueTypes::TYPE_NUMBER) {
			bool result = m_typeDescription.m_typeData.m_number.m_precision == rhs.m_typeData.m_number.m_precision &&
				m_typeDescription.m_typeData.m_number.m_scale == rhs.m_typeData.m_number.m_scale &&
				m_typeDescription.m_typeData.m_number.m_nonNegative == rhs.m_typeData.m_number.m_nonNegative;
			if (!result)
				return false;
		}
		else if (valType == eValueTypes::TYPE_DATE) {
			bool result = m_typeDescription.m_typeData.m_date.m_dateTime == rhs.m_typeData.m_date.m_dateTime;
			if (!result)
				return false;
		}
		else if (valType == eValueTypes::TYPE_STRING) {
			bool result = m_typeDescription.m_typeData.m_string.m_length == rhs.m_typeData.m_string.m_length &&
				m_typeDescription.m_typeData.m_string.m_allowedLength == rhs.m_typeData.m_string.m_allowedLength;
			if (!result)
				return false;
		}

		return ContainType(clsid);
	}
};

class BACKEND_API ITypeAttribute : public ITypeWrapper {
public:

	ITypeAttribute() :
		ITypeWrapper() {
	}

	ITypeAttribute(const ITypeAttribute& src) :
		ITypeWrapper(src) {
	}

	ITypeAttribute(const eValueTypes& defType) :
		ITypeWrapper(defType) {
	}

	ITypeAttribute(const class_identifier_t& clsid) :
		ITypeWrapper(clsid) {
	}

	ITypeAttribute(const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr) :
		ITypeWrapper(clsid, descr) {
	}

	ITypeAttribute(const std::set<class_identifier_t>& clsids) :
		ITypeWrapper(clsids) {
	}

	ITypeAttribute(const std::set<class_identifier_t>& clsids, const typeDescription_t::typeData_t& descr) :
		ITypeWrapper(clsids, descr) {
	}

	//Create value by selected type
	virtual CValue CreateValue() const;
	virtual CValue* CreateValueRef() const;

	//convert value
	template<class retType = CValue>
	retType* CreateAndConvertValueRef() {
		CValue* retVal = CreateValueRef();
		if (retVal != nullptr)
			return value_cast<retType>(retVal);
		return (retType*)nullptr;
	}

	//Adjust value
	virtual CValue AdjustValue() const;
	virtual CValue AdjustValue(const CValue& varValue) const;

	//get metaData
	virtual IMetaData* GetMetaData() const = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////

class BACKEND_API wxVariantAttributeData : public wxVariantData,
	public ITypeWrapper {
	wxString MakeString() const;
protected:
	virtual void DoSetFromMetaId(const meta_identifier_t& id);
public:

	/////////////////////////////////////////////////////////////////////////////////////////

	void RefreshData(const meta_identifier_t& id) {
		DoSetFromMetaId(id);
	}

	/////////////////////////////////////////////////////////////////////////////////////////

	wxVariantAttributeData(IMetaData* metaData) : wxVariantData(),
		ITypeWrapper(),
		m_metaData(metaData) {
	}

	wxVariantAttributeData(IMetaData* metaData, const meta_identifier_t& id) : wxVariantData(),
		ITypeWrapper(),
		m_metaData(metaData) {
		DoSetFromMetaId(id);
	}

	wxVariantAttributeData(const wxVariantAttributeData& list) : wxVariantData(),
		ITypeWrapper(list),
		m_metaData(list.m_metaData)
	{
	}

	virtual bool Eq(wxVariantData& data) const {
		wxVariantAttributeData* srcAttr = dynamic_cast<wxVariantAttributeData*>(&data);
		if (srcAttr != nullptr)
			return srcAttr->GetTypeDescription() == GetTypeDescription();
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const {
		return wxT("wxVariantAttributeData");
	}

	IMetaData* GetMetaData() const {
		return m_metaData;
	}

protected:
	IMetaData* m_metaData;
};

#endif