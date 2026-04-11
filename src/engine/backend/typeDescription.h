#ifndef __TYPE_DESCRIPTION_H__
#define __TYPE_DESCRIPTION_H__

enum ibDateFractions {
	ibDateFractions_Date = 0,
	ibDateFractions_DateTime,
	ibDateFractions_Time
};

enum ibAllowedLength {
	ibAllowedLength_Variable,
	ibAllowedLength_Fixed
};

struct ibQualifierNumber {
	bool m_nonNegative;
	unsigned char m_precision;
	unsigned char m_scale;
	ibQualifierNumber(unsigned char precision = 10, char scale = 0, bool nonNegative = false) :
		m_nonNegative(nonNegative), m_precision(precision), m_scale(scale) {
	}
};

struct ibQualifierDate {
	ibDateFractions m_dateTime;
	ibQualifierDate(ibDateFractions dateTime = ibDateFractions::ibDateFractions_DateTime) : m_dateTime(dateTime) {
	}
};

struct ibQualifierString {
	unsigned short m_length;
	ibAllowedLength m_allowedLength;
	ibQualifierString(unsigned short length = 10, ibAllowedLength allowedLength = ibAllowedLength::ibAllowedLength_Variable) :
		m_length(length), m_allowedLength(allowedLength) {
	}
};

#include "backend/compiler/value.h"

struct ibTypeDescription {

	std::vector<ibClassID> m_listTypeClass;

	struct ibTypeData {
		ibQualifierNumber m_number;
		ibQualifierDate	 m_date;
		ibQualifierString m_string;
	public:

		ibTypeData() :
			m_number(10, 0), m_date(ibDateFractions::ibDateFractions_Date), m_string(10) {
		} //empty 
		ibTypeData(unsigned char precision, unsigned char scale, bool nonnegative = false) :
			m_number(precision, scale, nonnegative), m_date(ibDateFractions::ibDateFractions_Date), m_string(10) {
		}
		ibTypeData(ibDateFractions dateTime) :
			m_number(10, 0), m_date(dateTime), m_string(10) {
		}
		ibTypeData(unsigned short length, ibAllowedLength allowedLength = ibAllowedLength::ibAllowedLength_Variable) :
			m_number(10, 0), m_date(ibDateFractions::ibDateFractions_Date), m_string(length, allowedLength) {
		}
		ibTypeData(const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, const ibQualifierString& qString) :
			m_number(qNumber), m_date(qDate), m_string(qString) {
		}

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
		ibDateFractions GetDateFraction() const {
			return m_date.m_dateTime;
		}

		//get special data string  
		unsigned short GetLength() const {
			return m_string.m_length;
		}

		ibAllowedLength GetAllowedLength() const {
			return m_string.m_allowedLength;
		}

		void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false) {
			m_number.m_precision = precision;
			m_number.m_scale = scale;
			m_number.m_nonNegative = nonNegative;
		}

		void SetDate(ibDateFractions dateTime) {
			m_date.m_dateTime = dateTime;
		}

		void SetString(unsigned short length, ibAllowedLength allowedLength = ibAllowedLength::ibAllowedLength_Variable) {
			m_string.m_length = length;
			m_string.m_allowedLength = allowedLength;
		}
	};

	ibTypeData m_typeData;

public:

	bool IsOk() const { return m_listTypeClass.size() > 0; }

	ibClassID GetFirstClsid() const {
		if (m_listTypeClass.size() == 0)
			return 0;
		auto it = m_listTypeClass.begin();
		std::advance(it, 0);
		return *it;
	}

	const std::vector<ibClassID>& GetClsidList() const { return m_listTypeClass; }

	//get special data number 
	unsigned char GetPrecision() const { return m_typeData.GetPrecision(); }
	unsigned char GetScale() const { return m_typeData.GetScale(); }

	bool IsNonNegative() const { return m_typeData.IsNonNegative(); }

	//get special data date  
	ibDateFractions GetDateFraction() const { return m_typeData.GetDateFraction(); }

	//get special data string  
	unsigned short GetLength() const { return m_typeData.GetLength(); }
	ibAllowedLength GetAllowedLength() const { return m_typeData.GetAllowedLength(); }

	////////////////////////////////////////////////////////////////////////////////////////////////

	void SetDefaultMetaType(const ibTypeDescription& typeDesc) {
		ClearMetaType();
		AppendMetaType(typeDesc.m_listTypeClass, typeDesc.m_typeData);
	}

	void SetDefaultMetaType(const ibValueTypes& valType) {
		ClearMetaType();
		AppendMetaType(valType);
	}

	void SetDefaultMetaType(const ibClassID& clsid) {
		ClearMetaType();
		AppendMetaType(clsid);
	}

	void SetDefaultMetaType(const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr) {
		ClearMetaType();
		AppendMetaType(clsid, descr);
	}

	void SetDefaultMetaType(const std::vector<ibClassID>& array) {
		ClearMetaType();
		AppendMetaType(array);
	}

	void SetDefaultMetaType(const std::vector<ibClassID>& array, const ibTypeDescription::ibTypeData& descr) {
		ClearMetaType();
		AppendMetaType(array, descr.m_number, descr.m_date, descr.m_string);
	}

	void SetDefaultMetaType(const std::vector<ibClassID>& array,
		const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, ibQualifierString& qString) {
		ClearMetaType();
		AppendMetaType(array, qNumber, qDate, qString);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void AppendMetaType(const ibValueTypes& valType) {

		if (valType == ibValueTypes::TYPE_NUMBER) m_typeData.SetNumber(10, 0);
		if (valType == ibValueTypes::TYPE_DATE) m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
		if (valType == ibValueTypes::TYPE_STRING) m_typeData.SetString(10);

		const ibClassID clsid = ibValue::GetIDByVT(valType);
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
		if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
	}

	void AppendMetaType(const ibClassID& clsid) {

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER)) m_typeData.SetNumber(10, 0);
		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_DATE)) m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_STRING)) m_typeData.SetString(10);

		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
		if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
	}

	void AppendMetaType(const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr) {

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER)) {
			m_typeData.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		}

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_DATE)) {
			m_typeData.SetDate(descr.GetDateFraction());
		}

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_STRING)) {
			m_typeData.SetString(descr.GetLength(), descr.GetAllowedLength());
		}

		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
		if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
	}

	void AppendMetaType(const std::vector<ibClassID>& array) {

		auto iterator_number = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		if (iterator_number != array.end()) m_typeData.SetNumber(10, 0);
		auto iterator_date = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		if (iterator_date != array.end()) m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
		auto iterator_string = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		if (iterator_string != array.end()) m_typeData.SetString(10);

		for (auto clsid : array) {
			auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
			if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
		}
	}

	void AppendMetaType(const std::vector<ibClassID>& array, const ibTypeDescription::ibTypeData& descr) {

		auto iterator_number = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		if (iterator_number != array.end()) m_typeData.SetNumber(descr.GetPrecision(), descr.GetScale(), descr.IsNonNegative());
		auto iterator_date = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		if (iterator_date != array.end()) m_typeData.SetDate(descr.GetDateFraction());
		auto iterator_string = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		if (iterator_string != array.end()) m_typeData.SetString(descr.GetLength(), descr.GetAllowedLength());

		for (auto clsid : array) {
			auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
			if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
		}
	}

	void AppendMetaType(const std::vector<ibClassID>& array,
		const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, const ibQualifierString& qString) {

		auto iterator_number = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		if (iterator_number != array.end()) m_typeData.SetNumber(qNumber.m_precision, qNumber.m_scale);
		auto iterator_date = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		if (iterator_date != array.end()) m_typeData.SetDate(qDate.m_dateTime);
		auto iterator_string = std::find(array.begin(), array.end(), ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		if (iterator_string != array.end()) m_typeData.SetString(qString.m_length);

		for (auto clsid : array) {
			auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
			if (iterator == m_listTypeClass.end()) m_listTypeClass.emplace_back(clsid);
		}
	}

	void AppendMetaType(const ibTypeDescription& typeDesc) {
		AppendMetaType(typeDesc.m_listTypeClass, typeDesc.m_typeData);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearMetaType() {
		m_typeData.SetNumber(10, 0);
		m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
		m_typeData.SetString(10);
		m_listTypeClass.clear();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	void ClearMetaType(const ibClassID& clsid) {

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER)) {
			m_typeData.SetNumber(10, 0);
		}

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_DATE)) {
			m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
		}

		if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_STRING)) {
			m_typeData.SetString(10);
		}

		m_listTypeClass.erase(
			std::remove(m_listTypeClass.begin(), m_listTypeClass.end(), clsid),
			m_listTypeClass.end());
	}

	void ClearMetaType(const std::vector<ibClassID>& array) {

		for (const auto clsid : array) {

			if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER)) {
				m_typeData.SetNumber(10, 0);
			}
			if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_DATE)) {
				m_typeData.SetDate(ibDateFractions::ibDateFractions_DateTime);
			}
			if (clsid == ibValue::GetIDByVT(ibValueTypes::TYPE_STRING)) {
				m_typeData.SetString(10);
			}

			m_listTypeClass.erase(
				std::remove(m_listTypeClass.begin(), m_listTypeClass.end(), clsid),
				m_listTypeClass.end());
		}
	}

	void ClearMetaType(const ibTypeDescription& typeDesc) { ClearMetaType(typeDesc.m_listTypeClass); }

	//////////////////////////////////////////////////
	void SetTypeData(const ibTypeDescription::ibTypeData& typeData) { m_typeData = typeData; }
	const ibTypeDescription::ibTypeData& GetTypeData() const { return m_typeData; }
	//////////////////////////////////////////////////

	void SetNumber(unsigned char precision, unsigned char scale, bool nonNegative = false) {
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(),
			ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		if (iterator == m_listTypeClass.end())
			m_listTypeClass.emplace_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		m_typeData.SetNumber(precision, scale, nonNegative);
	}

	void SetDate(ibDateFractions dateTime) {
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(),
			ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		if (iterator == m_listTypeClass.end())
			m_listTypeClass.emplace_back(ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		m_typeData.SetDate(dateTime);
	}

	void SetString(unsigned short length, ibAllowedLength allowedLength = ibAllowedLength::ibAllowedLength_Variable) {
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(),
			ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		if (iterator == m_listTypeClass.end())
			m_listTypeClass.emplace_back(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		m_typeData.SetString(length, allowedLength);
	}

	//qualifiers 
	const ibQualifierNumber& GetNumberQualifier() const { return m_typeData.m_number; }
	const ibQualifierDate& GetDateQualifier() const { return m_typeData.m_date; }
	const ibQualifierString& GetStringQualifier() const { return m_typeData.m_string; }

	//////////////////////////////////////////////////

	ibClassID GetByIdx(unsigned int idx) const {
		if (idx > m_listTypeClass.size())
			return wxNOT_FOUND;
		return m_listTypeClass[idx];
	}

	//////////////////////////////////////////////////

	unsigned int GetClsidCount() const { return m_listTypeClass.size(); }

	//////////////////////////////////////////////////

	ibTypeDescription() {}
	ibTypeDescription(const ibClassID& clsid) : m_listTypeClass({ clsid }) {}
	ibTypeDescription(const ibClassID& clsid, const ibTypeData& descr) : m_listTypeClass({ clsid }), m_typeData(descr) {}
	ibTypeDescription(const ibClassID& clsid, const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, const ibQualifierString& qString) : m_listTypeClass({ clsid }), m_typeData(qNumber, qDate, qString) {}
	ibTypeDescription(const std::vector<ibClassID>& array) : m_listTypeClass(array) {}
	ibTypeDescription(const std::vector<ibClassID>& array, const ibTypeData& descr) : m_listTypeClass(array), m_typeData(descr) {}
	ibTypeDescription(const std::vector<ibClassID>& array, const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, const ibQualifierString& qString) : m_listTypeClass(array), m_typeData(qNumber, qDate, qString) {}

	bool ContainType(const ibValueTypes& valType) const {
		if (valType == ibValueTypes::TYPE_ENUM) {
			for (auto clsid : m_listTypeClass) {
				if (ibValue::IsRegisterCtor(clsid)) {
					if (ibValue::GetVTByID(clsid) == ibValueTypes::TYPE_ENUM)
						return true;
				}
			}
			return false;
		}
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), ibValue::GetIDByVT(valType));
		return iterator != m_listTypeClass.end();
	}

	bool ContainType(const ibClassID& clsid) const {
		auto iterator = std::find(m_listTypeClass.begin(), m_listTypeClass.end(), clsid);
		return iterator != m_listTypeClass.end();
	}

	bool EqualType(const ibClassID& clsid, const ibTypeDescription& rhs) const {
		ibValueTypes valType = ibValue::GetVTByID(clsid);
		if (valType == ibValueTypes::TYPE_NUMBER) {
			bool result = m_typeData.m_number.m_precision == rhs.m_typeData.m_number.m_precision &&
				m_typeData.m_number.m_scale == rhs.m_typeData.m_number.m_scale &&
				m_typeData.m_number.m_nonNegative == rhs.m_typeData.m_number.m_nonNegative;
			if (!result)
				return false;
		}
		else if (valType == ibValueTypes::TYPE_DATE) {
			bool result = m_typeData.m_date.m_dateTime == rhs.m_typeData.m_date.m_dateTime;
			if (!result)
				return false;
		}
		else if (valType == ibValueTypes::TYPE_STRING) {
			bool result = m_typeData.m_string.m_length == rhs.m_typeData.m_string.m_length &&
				m_typeData.m_string.m_allowedLength == rhs.m_typeData.m_string.m_allowedLength;
			if (!result)
				return false;
		}

		return ContainType(clsid);
	}

	bool operator == (const ibTypeDescription& rhs) const {
		if (m_listTypeClass == rhs.m_listTypeClass) {
			for (auto clsid : m_listTypeClass) {
				ibValueTypes valType = ibValue::GetVTByID(clsid);
				if (valType == ibValueTypes::TYPE_NUMBER) {
					bool result = m_typeData.m_number.m_precision == rhs.m_typeData.m_number.m_precision &&
						m_typeData.m_number.m_scale == rhs.m_typeData.m_number.m_scale &&
						m_typeData.m_number.m_nonNegative == rhs.m_typeData.m_number.m_nonNegative;
					if (!result)
						return false;
				}
				else if (valType == ibValueTypes::TYPE_DATE) {
					bool result = m_typeData.m_date.m_dateTime == rhs.m_typeData.m_date.m_dateTime;
					if (!result)
						return false;
				}
				else if (valType == ibValueTypes::TYPE_STRING) {
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

	bool operator != (const ibTypeDescription& rhs) const {
		if (m_listTypeClass == rhs.m_listTypeClass) {
			for (auto clsid : m_listTypeClass) {
				ibValueTypes valType = ibValue::GetVTByID(clsid);
				if (valType == ibValueTypes::TYPE_NUMBER) {
					bool result = m_typeData.m_number.m_precision != rhs.m_typeData.m_number.m_precision ||
						m_typeData.m_number.m_scale != rhs.m_typeData.m_number.m_scale ||
						m_typeData.m_number.m_nonNegative != rhs.m_typeData.m_number.m_nonNegative;
					if (result)
						return true;

				}
				else if (valType == ibValueTypes::TYPE_DATE) {
					bool result = m_typeData.m_date.m_dateTime != rhs.m_typeData.m_date.m_dateTime;
					if (result)
						return true;
				}
				else if (valType == ibValueTypes::TYPE_STRING) {
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

	bool LoadMetaType(const ibTypeDescription& rhs) {
		ClearMetaType();
		m_typeData = rhs.m_typeData;
		m_listTypeClass = rhs.m_listTypeClass;
		return true;
	}
};

class BACKEND_API ibTypeDescriptionMemory {
public:
	//load & save object in control 
	static bool LoadData(class ibReaderMemory& reader, ibTypeDescription& typeDesc);
	static bool SaveData(class ibWriterMemory& writer, ibTypeDescription& typeDesc);
};

struct ibMetaDescription {
	std::vector<ibMetaID> m_listMetaClass;
public:
	ibMetaDescription() {}
	ibMetaDescription(const ibMetaID& id) : m_listMetaClass({ id }) {}
	ibMetaDescription(const std::vector<ibMetaID>& array) : m_listMetaClass(array) {}
public:

	bool IsOk() const { return m_listMetaClass.size() > 0; }

	void SetDefaultMetaType(const ibMetaID& id) {
		ClearMetaType();
		AppendMetaType(id);
	}

	void SetDefaultMetaType(const ibMetaDescription& typeDesc) {
		ClearMetaType();
		AppendMetaType(typeDesc.m_listMetaClass);
	}

	void AppendMetaType(const ibMetaID& id) { m_listMetaClass.emplace_back(id); }
	void AppendMetaType(const std::vector<ibMetaID>& array) { for (auto& id : array) m_listMetaClass.emplace_back(id); }

	void ClearMetaType() { m_listMetaClass.clear(); }
	bool ContainMetaType(const ibMetaID& id) const {
		auto iterator = std::find(m_listMetaClass.begin(), m_listMetaClass.end(), id);
		return iterator != m_listMetaClass.end();
	}

	ibMetaID GetByIdx(unsigned int idx) const {
		if (idx > m_listMetaClass.size())
			return wxNOT_FOUND;
		return m_listMetaClass[idx];
	}

	unsigned int GetTypeCount() const { return m_listMetaClass.size(); }
};

class BACKEND_API ibMetaDescriptionMemory {
public:
	//load & save object in control 
	static bool LoadData(class ibReaderMemory& reader, ibMetaDescription& metaDesc);
	static bool SaveData(class ibWriterMemory& writer, ibMetaDescription& metaDesc);
};

#endif