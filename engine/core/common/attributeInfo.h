#ifndef _ATTRIBUTE_INFO_H__
#define _ATTRIBUTE_INFO_H__

enum eDateFractions
{
	eDate = 0,
	eDateTime,
	eTime
};

class CValueTypeDescription;

class CValueQualifierNumber : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueQualifierNumber);
public:
	unsigned char m_precision;
	unsigned char m_scale;
public:

	virtual wxString GetTypeString() const { return wxT("qualifierNumber"); }
	virtual wxString GetString() const { return wxT("qualifierNumber"); }

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

	virtual wxString GetTypeString() const { return wxT("qualifierDate"); }
	virtual wxString GetString() const { return wxT("qualifierDate"); }

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

	virtual wxString GetTypeString() const { return wxT("qualifierString"); }
	virtual wxString GetString() const { return wxT("qualifierString"); }

	CValueQualifierString() : CValue(eValueTypes::TYPE_VALUE, true) {}
	CValueQualifierString(unsigned short length) : CValue(eValueTypes::TYPE_VALUE, true),
		m_length(length)
	{
	}
};

class Property;
class OptionList;

class IAttributeInfo {
public:

	IAttributeInfo(eValueTypes defType = eValueTypes::TYPE_STRING) : m_typeDescription(defType) {}

	//get ref type 
	virtual meta_identifier_t GetTypeObject() const { return m_typeDescription.GetTypeObject(); }
	virtual CLASS_ID GetClassTypeObject() const;

	//get special data number 
	virtual unsigned char GetPrecision() const { return m_typeDescription.GetPrecision(); }
	virtual unsigned char GetScale() const { return m_typeDescription.GetScale(); }

	//get special data date  
	virtual eDateFractions GetDateTime() const { return m_typeDescription.GetDateTime(); }

	//get special data string  
	unsigned short GetLength() const { return m_typeDescription.GetLength(); }

	//get metadata
	virtual IMetadata *GetMetadata() const = 0;

	//qualifers:
	CValueQualifierNumber *GetNumberQualifier() const {
		if (m_typeDescription.GetTypeObject() != eValueTypes::TYPE_NUMBER)
			return NULL;
		return new CValueQualifierNumber(m_typeDescription.GetPrecision(), m_typeDescription.GetScale());
	}

	CValueQualifierDate *GetDateQualifier() const {
		if (m_typeDescription.GetTypeObject() != eValueTypes::TYPE_DATE)
			return NULL;
		return new CValueQualifierDate(m_typeDescription.GetDateTime());
	}

	CValueQualifierString *GetStringQualifier() const {
		if (m_typeDescription.GetTypeObject() != eValueTypes::TYPE_STRING)
			return NULL;
		return new CValueQualifierString(m_typeDescription.GetLength());
	}

	void SetDefaultMetatype(meta_identifier_t id) {
		m_typeDescription.SetDefaultMetatype(id);
	}

	void SetMetatype(meta_identifier_t id, CValueQualifierNumber *qNumber, CValueQualifierDate *qDate, CValueQualifierString *qString) {
		m_typeDescription.SetMetatype(id, qNumber, qDate, qString);
	}

protected:

	class typeDescription_t {
		meta_identifier_t m_typeObject;
		union metaDescription_t {
			eDateFractions m_dateTime;
			struct typeNumber_t {
				unsigned char m_precision;
				unsigned char m_scale;
				typeNumber_t(unsigned char precision, char scale) : m_precision(precision), m_scale(scale) {}
			} m_number;
			unsigned short m_length = 10;

			metaDescription_t() {} //empty 
			metaDescription_t(unsigned char precision, unsigned char scale) : m_number(precision, scale) {}
			metaDescription_t(eDateFractions dateTime) : m_dateTime(dateTime) {}
			metaDescription_t(unsigned short length) : m_length(length) {}

			//get special data number 
			unsigned char GetPrecision() const { return m_number.m_precision; }
			unsigned char GetScale() const { return m_number.m_scale; }
			//get special data date  
			eDateFractions GetDateTime() const { return m_dateTime; }
			//get special data string  
			unsigned short GetLength() const { return m_length; }

			void SetNumber(unsigned char precision, unsigned char scale) { m_number.m_precision = precision; m_number.m_scale = scale; }
			void SetDate(eDateFractions dateTime) { m_dateTime = dateTime; }
			void SetString(unsigned short length) { m_length = length; }
		} m_metaDescription;

	public:

		bool operator == (const typeDescription_t& rhs) const
		{
			if (m_typeObject == rhs.m_typeObject) {

				if (m_typeObject == eValueTypes::TYPE_NUMBER) {
					return m_metaDescription.m_number.m_precision == rhs.m_metaDescription.m_number.m_precision &&
						m_metaDescription.m_number.m_scale == rhs.m_metaDescription.m_number.m_scale;
				}
				else if (m_typeObject == eValueTypes::TYPE_DATE) {
					return m_metaDescription.m_dateTime == rhs.m_metaDescription.m_dateTime;
				}
				else if (m_typeObject == eValueTypes::TYPE_STRING) {
					return m_metaDescription.m_length == rhs.m_metaDescription.m_length;
				}

				return true;
			}

			return false;
		}

		bool operator != (const typeDescription_t& rhs) const
		{
			if (m_typeObject == rhs.m_typeObject) {

				if (m_typeObject == eValueTypes::TYPE_NUMBER) {
					return m_metaDescription.m_number.m_precision != rhs.m_metaDescription.m_number.m_precision ||
						m_metaDescription.m_number.m_scale != rhs.m_metaDescription.m_number.m_scale;
				}
				else if (m_typeObject == eValueTypes::TYPE_DATE) {
					return m_metaDescription.m_dateTime != rhs.m_metaDescription.m_dateTime;
				}
				else if (m_typeObject == eValueTypes::TYPE_STRING) {
					return m_metaDescription.m_length != rhs.m_metaDescription.m_length;
				}

				return false;
			}

			return true;
		}

		meta_identifier_t GetTypeObject() const { return m_typeObject; }
		//get special data number 
		unsigned char GetPrecision() const { return m_metaDescription.GetPrecision(); }
		unsigned char GetScale() const { return m_metaDescription.GetScale(); }
		//get special data date  
		eDateFractions GetDateTime() const { return m_metaDescription.GetDateTime(); }
		//get special data string  
		unsigned short GetLength() const { return m_metaDescription.GetLength(); }

		void SetDefaultMetatype(meta_identifier_t typeObject)
		{
			if (typeObject == eValueTypes::TYPE_NUMBER) {
				m_metaDescription.SetNumber(10, 0);
			}
			else if (typeObject == eValueTypes::TYPE_DATE) {
				m_metaDescription.SetDate(eDateFractions::eDateTime);
			}
			else if (typeObject == eValueTypes::TYPE_STRING) {
				m_metaDescription.SetString(10);
			}
			m_typeObject = typeObject;
		}

		void SetMetatype(meta_identifier_t typeObject, CValueQualifierNumber *qNumber, CValueQualifierDate *qDate, CValueQualifierString *qString) {
			if (typeObject == eValueTypes::TYPE_NUMBER) {
				m_metaDescription.SetNumber(qNumber ? qNumber->m_precision : 10, qNumber ? qNumber->m_scale : 0);
			}
			else if (typeObject == eValueTypes::TYPE_DATE) {
				m_metaDescription.SetDate(qDate ? qDate->m_dateTime : eDateFractions::eDateTime);
			}
			else if (typeObject == eValueTypes::TYPE_STRING) {
				m_metaDescription.SetString(qString ? qString->m_length : 10);
			}
			m_typeObject = typeObject;
		}

		void SetNumber(unsigned char precision, unsigned char scale)
		{
			if (m_typeObject != eValueTypes::TYPE_NUMBER) {
				m_typeObject = eValueTypes::TYPE_NUMBER;
			}
			m_metaDescription.SetNumber(precision, scale);
		}
		void SetDate(eDateFractions dateTime)
		{
			if (m_typeObject != eValueTypes::TYPE_DATE) {
				m_typeObject = eValueTypes::TYPE_DATE;
			}
			m_metaDescription.SetDate(dateTime);
		}
		void SetString(unsigned short length)
		{
			if (m_typeObject != eValueTypes::TYPE_STRING) {
				m_typeObject = eValueTypes::TYPE_STRING;
			}
			m_metaDescription.SetString(length);
		}

		typeDescription_t(meta_identifier_t typeObject) { SetDefaultMetatype(typeObject); }

	} m_typeDescription;

public:

	typeDescription_t GetTypeDescription() const { 
		return m_typeDescription; 
	}
	
	CValueTypeDescription *GetValueTypeDescription() const;
};

#endif