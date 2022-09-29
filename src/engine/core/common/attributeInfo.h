#ifndef _ATTRIBUTE_INFO_H__
#define _ATTRIBUTE_INFO_H__

enum eDateFractions
{
	eDateFractions_Date = 0,
	eDateFractions_DateTime,
	eDateFractions_Time
};

enum eAllowedLength {
	eAllowedLength_Variable,
	eAllowedLength_Fixed
};

class CValueTypeDescription;

class CValueQualifierNumber : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierNumber);
public:
	unsigned char m_precision;
	unsigned char m_scale;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierNumber");
	}

	virtual wxString GetString() const {
		return wxT("qualifierNumber");
	}

	CValueQualifierNumber() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierNumber(unsigned char precision, unsigned char scale) : CValue(eValueTypes::TYPE_VALUE, true),
		m_precision(precision), m_scale(scale)
	{
	}
};

class CValueQualifierDate : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierDate);
public:
	eDateFractions m_dateTime;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierDate");
	}

	virtual wxString GetString() const {
		return wxT("qualifierDate");
	}

	CValueQualifierDate() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierDate(eDateFractions dateTime) : CValue(eValueTypes::TYPE_VALUE, true),
		m_dateTime(dateTime)
	{
	}
};

class CValueQualifierString : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierString);
public:
	unsigned short m_length;
public:

	virtual wxString GetTypeString() const {
		return wxT("qualifierString");
	}

	virtual wxString GetString() const {
		return wxT("qualifierString");
	}

	CValueQualifierString() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierString(unsigned short length) : CValue(eValueTypes::TYPE_VALUE, true),
		m_length(length)
	{
	}
};

class Property; class OptionList;

class CMemoryReader;
class CMemoryWriter;

////////////////////////////////////////////////////////////////////////////////

class IAttributeWrapper {
	std::set<CLASS_ID> m_clsids, m_defClsids;
protected:
	struct metaDescription_t {
		eDateFractions m_dateTime;
		struct typeNumber_t {
			bool m_nonNegative;
			unsigned char m_precision;
			unsigned char m_scale;
			typeNumber_t(unsigned char precision, char scale, bool nonNegative = false) : m_precision(precision), m_scale(scale), m_nonNegative(nonNegative) {}
		} m_number;

		struct typeString_t {
			unsigned short m_length;
			eAllowedLength m_allowedLength;
			typeString_t(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) : m_length(length), m_allowedLength(allowedLength) {}
		} m_string;

		metaDescription_t() : m_number(10, 0), m_dateTime(eDateFractions::eDateFractions_Date), m_string(10) {} //empty 
		metaDescription_t(unsigned char precision, unsigned char scale, bool nonnegative = false) : m_number(precision, scale, nonnegative), m_dateTime(eDateFractions::eDateFractions_Date), m_string(10) {}
		metaDescription_t(eDateFractions dateTime) : m_number(10, 0), m_dateTime(dateTime), m_string(10) {}
		metaDescription_t(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) : m_number(10, 0), m_dateTime(eDateFractions::eDateFractions_Date), m_string(length, allowedLength) {}

		//get special data number 
		unsigned char GetPrecision() const { return m_number.m_precision; }
		unsigned char GetScale() const { return m_number.m_scale; }
		bool IsNonNegative() const { return m_number.m_nonNegative; }
		//get special data date  
		eDateFractions GetDateTime() const { return m_dateTime; }
		//get special data string  
		unsigned short GetLength() const { return m_string.m_length; }
		eAllowedLength GetAllowedLength() const { return m_string.m_allowedLength; }

		void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false) { m_number.m_precision = precision; m_number.m_scale = scale; m_number.m_nonNegative = nonNegative; }
		void SetDate(eDateFractions dateTime) { m_dateTime = dateTime; }
		void SetString(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable) { m_string.m_length = length; m_string.m_allowedLength = allowedLength; }

	};
private:
	metaDescription_t m_metaDescription, m_metaDefDescription;
public:

	bool EqualsType(const CLASS_ID& clsid,
		const metaDescription_t& rhs) const {
		eValueTypes valType = CValue::GetVTByID(clsid);
		if (valType == eValueTypes::TYPE_NUMBER) {
			bool result = m_metaDescription.m_number.m_precision == rhs.m_number.m_precision &&
				m_metaDescription.m_number.m_scale == rhs.m_number.m_scale &&
				m_metaDescription.m_number.m_nonNegative == rhs.m_number.m_nonNegative;
			if (!result)
				return false;
		}
		else if (valType == eValueTypes::TYPE_DATE) {
			bool result = m_metaDescription.m_dateTime == rhs.m_dateTime;
			if (!result)
				return false;
		}
		else if (valType == eValueTypes::TYPE_STRING) {
			bool result = m_metaDescription.m_string.m_length == rhs.m_string.m_length &&
				m_metaDescription.m_string.m_allowedLength == rhs.m_string.m_allowedLength;
			if (!result)
				return false;
		}

		return ContainType(clsid);
	}

	bool operator == (const IAttributeWrapper& rhs) const
	{
		if (m_clsids == rhs.m_clsids) {

			for (auto clsid : m_clsids) {
				eValueTypes valType = CValue::GetVTByID(clsid);
				if (valType == eValueTypes::TYPE_NUMBER) {
					bool result = m_metaDescription.m_number.m_precision == rhs.m_metaDescription.m_number.m_precision &&
						m_metaDescription.m_number.m_scale == rhs.m_metaDescription.m_number.m_scale &&
						m_metaDescription.m_number.m_nonNegative == rhs.m_metaDescription.m_number.m_nonNegative;
					if (!result)
						return false;
				}
				else if (valType == eValueTypes::TYPE_DATE) {
					bool result = m_metaDescription.m_dateTime == rhs.m_metaDescription.m_dateTime;
					if (!result)
						return false;
				}
				else if (valType == eValueTypes::TYPE_STRING) {
					bool result = m_metaDescription.m_string.m_length == rhs.m_metaDescription.m_string.m_length &&
						m_metaDescription.m_string.m_allowedLength == rhs.m_metaDescription.m_string.m_allowedLength;
					if (!result)
						return false;
				}
			}

			return true;
		}

		return false;
	}

	bool operator != (const IAttributeWrapper& rhs) const
	{
		if (m_clsids == rhs.m_clsids) {

			for (auto clsid : m_clsids) {
				eValueTypes valType = CValue::GetVTByID(clsid);
				if (valType == eValueTypes::TYPE_NUMBER) {
					bool result = m_metaDescription.m_number.m_precision != rhs.m_metaDescription.m_number.m_precision ||
						m_metaDescription.m_number.m_scale != rhs.m_metaDescription.m_number.m_scale ||
						m_metaDescription.m_number.m_nonNegative != rhs.m_metaDescription.m_number.m_nonNegative;
					if (result)
						return true;

				}
				else if (valType == eValueTypes::TYPE_DATE) {
					bool result = m_metaDescription.m_dateTime != rhs.m_metaDescription.m_dateTime;
					if (result)
						return true;
				}
				else if (valType == eValueTypes::TYPE_STRING) {
					bool result = m_metaDescription.m_string.m_length != rhs.m_metaDescription.m_string.m_length ||
						m_metaDescription.m_string.m_allowedLength != rhs.m_metaDescription.m_string.m_allowedLength;
					if (result)
						return true;
				}
			}

			return false;
		}

		return true;
	}

	virtual CLASS_ID GetFirstClsid() const {
		if (m_clsids.size() == 0)
			return 0;
		auto itFounded = m_clsids.begin();
		std::advance(itFounded, 0);
		return *itFounded;
	}

	virtual std::set<CLASS_ID> GetClsids() const {
		return m_clsids;
	}

	virtual bool ContainType(const eValueTypes& valType) const {
		if (valType == eValueTypes::TYPE_ENUM) {
			for (auto clsid : m_clsids) {
				if (CValue::IsRegisterObject(clsid)) {
					ISimpleObjectValueSingle* singleObject =
						dynamic_cast<ISimpleObjectValueSingle*>(CValue::GetAvailableObject(clsid));
					if (singleObject != NULL) {
						if (singleObject->GetValueType() == eValueTypes::TYPE_ENUM) {
							return true;
						}
					}
				}
			}
			return false;
		}
		return m_clsids.find(CValue::GetIDByVT(valType)) != m_clsids.end();
	}

	virtual bool ContainType(const CLASS_ID& clsid) const {
		return m_clsids.find(clsid) != m_clsids.end();
	}

	virtual CLASS_ID GetByIdx(unsigned int idx) const {
		if (m_clsids.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_clsids.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	//get special data number 
	unsigned char GetPrecision() const {
		return m_metaDescription.GetPrecision();
	}

	unsigned char GetScale() const {
		return m_metaDescription.GetScale();
	}

	bool IsNonNegative() const {
		return m_metaDescription.IsNonNegative();
	}

	//get special data date  
	eDateFractions GetDateTime() const {
		return m_metaDescription.GetDateTime();
	}

	//get special data string  
	unsigned short GetLength() const {
		return m_metaDescription.GetLength();
	}

	eAllowedLength GetAllowedLength() const {
		return m_metaDescription.GetAllowedLength();
	}

	void SetDefaultMetatype(const eValueTypes& valType)
	{
		m_clsids.clear();
		SetMetatype(valType);

		m_defClsids.clear();
		if (valType == eValueTypes::TYPE_NUMBER) {
			m_metaDefDescription.SetNumber(10, 0);
		}
		if (valType == eValueTypes::TYPE_DATE) {
			m_metaDefDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (valType == eValueTypes::TYPE_STRING) {
			m_metaDefDescription.SetString(10);
		}
		m_defClsids.insert(
			CValue::GetIDByVT(valType)
		);
	}

	void SetDefaultMetatype(const CLASS_ID& clsid)
	{
		m_clsids.clear();
		SetMetatype(clsid);
		m_defClsids.clear();
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_metaDefDescription.SetNumber(10, 0);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_metaDefDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_metaDefDescription.SetString(10);
		}
		m_defClsids.insert(clsid);
	}

	void SetDefaultMetatype(const CLASS_ID& clsid, const metaDescription_t& descr)
	{
		m_clsids.clear();
		SetMetatype(clsid, descr);
		m_defClsids.clear();
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_metaDefDescription.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_metaDefDescription.SetDate(descr.GetDateTime());
		}
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_metaDefDescription.SetString(descr.GetLength(), descr.GetAllowedLength());
		}
		m_defClsids.insert(clsid);
	}

	void SetDefaultMetatype(const std::set<CLASS_ID>& clsids)
	{
		m_clsids.clear();
		SetMetatype(clsids);
		m_defClsids.clear();
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDefDescription.SetNumber(10, 0);
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDefDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDefDescription.SetString(10);
		}
		for (auto clsid : clsids) {
			m_defClsids.insert(clsid);
		}
	}

	void SetDefaultMetatype(const std::set<CLASS_ID>& clsids, const metaDescription_t& descr)
	{
		m_clsids.clear();
		SetMetatype(clsids, descr);
		m_defClsids.clear();
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDefDescription.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDefDescription.SetDate(descr.GetDateTime());
		}
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDefDescription.SetString(descr.GetLength(), descr.GetAllowedLength());
		}
		for (auto clsid : clsids) {
			m_defClsids.insert(clsid);
		}
	}

	void SetDefaultMetatype(const std::set<CLASS_ID>& clsids,
		CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString)
	{
		m_clsids.clear();
		SetMetatype(clsids,
			qNumber, qDate, qString
		);
		m_defClsids.clear();
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDefDescription.SetNumber(qNumber ? qNumber->m_precision : 10, qNumber ? qNumber->m_scale : 0);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDefDescription.SetDate(qDate ? qDate->m_dateTime : eDateFractions::eDateFractions_DateTime);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDefDescription.SetString(qString ? qString->m_length : 10);
		}
		for (auto clsid : clsids) {
			m_defClsids.insert(clsid);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void SetMetatype(const eValueTypes& valType)
	{
		if (valType == eValueTypes::TYPE_NUMBER) {
			m_metaDescription.SetNumber(10, 0);
		}

		if (valType == eValueTypes::TYPE_DATE) {
			m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}

		if (valType == eValueTypes::TYPE_STRING) {
			m_metaDescription.SetString(10);
		}

		m_clsids.insert(
			CValue::GetIDByVT(valType)
		);
	}

	void SetMetatype(const CLASS_ID& clsid)
	{
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_metaDescription.SetNumber(10, 0);
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_metaDescription.SetString(10);
		}

		m_clsids.insert(clsid);
	}

	void SetMetatype(const CLASS_ID& clsid, const metaDescription_t& descr)
	{
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_metaDescription.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_metaDescription.SetDate(descr.GetDateTime());
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_metaDescription.SetString(descr.GetLength(), descr.GetAllowedLength());
		}

		m_clsids.insert(clsid);
	}

	void SetMetatype(const std::set<CLASS_ID>& clsids)
	{
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDescription.SetNumber(10, 0);
		}

		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}

		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDescription.SetString(10);
		}

		for (auto clsid : clsids) {
			m_clsids.insert(clsid);
		}
	}

	void SetMetatype(const std::set<CLASS_ID>& clsids, const metaDescription_t& descr)
	{
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDescription.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}

		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDescription.SetDate(descr.GetDateTime());
		}

		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDescription.SetString(descr.GetLength(), descr.GetAllowedLength());
		}

		for (auto clsid : clsids) {
			m_clsids.insert(clsid);
		}
	}

	void SetMetatype(const std::set<CLASS_ID>& clsids,
		CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString)
	{
		if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) != clsids.end()) {
			m_metaDescription.SetNumber(qNumber ? qNumber->m_precision : 10, qNumber ? qNumber->m_scale : 0);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE)) != clsids.end()) {
			m_metaDescription.SetDate(qDate ? qDate->m_dateTime : eDateFractions::eDateFractions_DateTime);
		}
		else if (clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING)) != clsids.end()) {
			m_metaDescription.SetString(qString ? qString->m_length : 10);
		}

		for (auto clsid : clsids) {
			m_clsids.insert(clsid);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearAllMetatype() {

		m_metaDescription.SetNumber(10, 0);
		m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		m_metaDescription.SetString(10);

		m_clsids.clear();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearMetatype(const CLASS_ID& clsid)
	{
		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
			m_metaDescription.SetNumber(10, 0);
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
			m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
		}

		if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
			m_metaDescription.SetString(10);
		}

		m_clsids.erase(clsid);
	}

	void ClearMetatype(const std::set<CLASS_ID>& clsids)
	{
		for (auto clsid : clsids) {

			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_NUMBER)) {
				m_metaDescription.SetNumber(10, 0);
			}

			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_DATE)) {
				m_metaDescription.SetDate(eDateFractions::eDateFractions_DateTime);
			}

			if (clsid == CValue::GetIDByVT(eValueTypes::TYPE_STRING)) {
				m_metaDescription.SetString(10);
			}

			m_clsids.erase(clsid);
		}
	}

	//////////////////////////////////////////////////

	virtual bool LoadTypeData(CMemoryReader& dataReader);
	virtual bool LoadFromVariant(const wxVariant& variant);
	virtual bool SaveTypeData(CMemoryWriter& dataWritter);
	virtual void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//////////////////////////////////////////////////

	void SetDescription(const metaDescription_t& metaDescription) {
		m_metaDescription = metaDescription;
	}

	//get meta decription 
	const metaDescription_t& GetDescription() const {
		return m_metaDescription;
	}

	//////////////////////////////////////////////////

	void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false)
	{
		auto foundedIt = m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER));

		if (foundedIt == m_clsids.end()) {
			m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_NUMBER));
		}

		m_metaDescription.SetNumber(precision, scale, nonNegative);
	}

	void SetDate(eDateFractions dateTime)
	{
		auto foundedIt = m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_DATE));

		if (foundedIt == m_clsids.end()) {
			m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_DATE));
		}

		m_metaDescription.SetDate(dateTime);
	}

	void SetString(unsigned short length, eAllowedLength allowedLength = eAllowedLength::eAllowedLength_Variable)
	{
		auto foundedIt = m_clsids.find(CValue::GetIDByVT(eValueTypes::TYPE_STRING));

		if (foundedIt == m_clsids.end()) {
			m_clsids.insert(CValue::GetIDByVT(eValueTypes::TYPE_STRING));
		}

		m_metaDescription.SetString(length, allowedLength);
	}

	//////////////////////////////////////////////////

	//get metadata
	virtual IMetadata* GetMetadata() const = 0;

	//////////////////////////////////////////////////

	unsigned int GetClsidCount() const {
		return m_clsids.size();
	}

	//////////////////////////////////////////////////

	IAttributeWrapper(const eValueTypes& valType) {
		SetDefaultMetatype(valType);
	}

	IAttributeWrapper(const CLASS_ID& clsid) {
		SetDefaultMetatype(clsid);
	}

	IAttributeWrapper(const std::set<CLASS_ID>& clsids) {
		SetDefaultMetatype(clsids);
	}

	IAttributeWrapper(const std::set<CLASS_ID>& clsids, const metaDescription_t& descr) {
		SetDefaultMetatype(clsids, descr);
	}

protected:

	IAttributeWrapper() {}

	IAttributeWrapper(const IAttributeWrapper& src) :
		m_clsids(src.m_clsids), m_metaDescription(src.m_metaDescription)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////

class IAttributeInfo :
	public IAttributeWrapper {
public:

	IAttributeInfo() :
		IAttributeWrapper()
	{
	}

	IAttributeInfo(const eValueTypes& defType) :
		IAttributeWrapper(defType)
	{
	}

	IAttributeInfo(const CLASS_ID& clsid) :
		IAttributeWrapper(clsid)
	{
	}

	IAttributeInfo(const std::set<CLASS_ID>& clsids) :
		IAttributeWrapper(clsids)
	{
	}

	IAttributeInfo(const std::set<CLASS_ID>& clsids, const metaDescription_t& descr) :
		IAttributeWrapper(clsids, descr)
	{
	}

	//Create value by selected type
	virtual CValue CreateValue() const;
	virtual CValue* CreateValueRef() const;

	//convert value
	template<class retType = CValue>
	retType* CreateAndConvertValueRef() {
		CValue* retVal = CreateValueRef();
		if (retVal != NULL)
			return value_cast<retType>(retVal);
		return (retType*)NULL;
	}

	//Adjust value
	virtual CValue AdjustValue() const;
	virtual CValue AdjustValue(const CValue& cVal) const;

	//Get data type 
	virtual CLASS_ID GetDataType() const;

	//get metadata
	virtual IMetadata* GetMetadata() const = 0;

	//qualifers:
	CValueQualifierNumber* GetNumberQualifier() const {
		if (!IAttributeWrapper::ContainType(eValueTypes::TYPE_NUMBER))
			return NULL;
		return new CValueQualifierNumber(IAttributeWrapper::GetPrecision(), IAttributeWrapper::GetScale());
	}

	CValueQualifierDate* GetDateQualifier() const {
		if (!IAttributeWrapper::ContainType(eValueTypes::TYPE_DATE))
			return NULL;
		return new CValueQualifierDate(IAttributeWrapper::GetDateTime());
	}

	CValueQualifierString* GetStringQualifier() const {
		if (!IAttributeWrapper::ContainType(eValueTypes::TYPE_STRING))
			return NULL;
		return new CValueQualifierString(IAttributeWrapper::GetLength());
	}

	const IAttributeWrapper& GetTypeDescription() const {
		return *this;
	}

	CValueTypeDescription* GetValueTypeDescription() const;
};

////////////////////////////////////////////////////////////////////////////////

class wxVariantAttributeData :
	public wxVariantData, public IAttributeWrapper {
	wxString MakeString() const;
protected:
	virtual void DoSetFromMetaId(const meta_identifier_t& id);
public:

	/////////////////////////////////////////////////////////////////////////////////////////

	void RefreshData(const meta_identifier_t& id) {
		DoSetFromMetaId(id);
	}

	/////////////////////////////////////////////////////////////////////////////////////////

	wxVariantAttributeData(IMetadata* metaData) : wxVariantData(),
		IAttributeWrapper(),
		m_metaData(metaData)
	{
	}

	wxVariantAttributeData(IMetadata* metaData, const meta_identifier_t& id) : wxVariantData(),
		IAttributeWrapper(),
		m_metaData(metaData)
	{
		DoSetFromMetaId(id);
	}

	wxVariantAttributeData(const wxVariantAttributeData& list) : wxVariantData(),
		IAttributeWrapper(list),
		m_metaData(list.m_metaData)
	{
	}

	virtual bool Eq(wxVariantData& data) const {
		wxVariantAttributeData* srcAttr = dynamic_cast<wxVariantAttributeData*>(&data);
		if (srcAttr != NULL) {
			return *srcAttr == *this;
		}
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

	IMetadata* GetMetadata() const {
		return m_metaData;
	}

protected:
	IMetadata* m_metaData;
};

#endif