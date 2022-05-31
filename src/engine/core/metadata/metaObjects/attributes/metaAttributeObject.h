#ifndef _ATTRIBUTES_H__
#define _ATTRIBUTES_H__

#include "metadata/metaObjects/metaObject.h"
#include "common/attributeInfo.h"

class IMetaAttributeObject : public IMetaObject,
	public IAttributeInfo {
	wxDECLARE_ABSTRACT_CLASS(IMetaAttributeObject);
public:

	enum eFieldTypes {
		eFieldTypes_Empty = 0,
		eFieldTypes_Boolean,
		eFieldTypes_Number,
		eFieldTypes_Date,
		eFieldTypes_String,
		eFieldTypes_Enum,
		eFieldTypes_Reference,
	};

	struct sqlField_t {

		wxString m_fieldTypeName;
		struct sqlData_t {
			eFieldTypes m_type;
			struct data_t {
				wxString m_fieldName;
				struct refData_t {
					wxString m_fieldRefType;
					wxString m_fieldRefName;
					refData_t() {
					}
					refData_t(const wxString& fieldRefType, const wxString& fieldRefName)
						: m_fieldRefType(fieldRefType), m_fieldRefName(fieldRefName) {
					}
					~refData_t() {
					}
				} m_fieldRefName;

				data_t()
					: m_fieldName(wxEmptyString)
				{
				}

				data_t(const wxString& fieldName)
					: m_fieldName(fieldName) {
				}

				data_t(const wxString& fieldRefType, const wxString& fieldRefNam)
					: m_fieldRefName(fieldRefType, fieldRefNam) {
				}

				~data_t() {
				}

			} m_field;

			sqlData_t() : m_type(eFieldTypes::eFieldTypes_Empty)
			{
			}
			sqlData_t(eFieldTypes type) : m_type(type)
			{
			}
			sqlData_t(eFieldTypes type, const wxString& fieldName) : m_type(type), m_field(fieldName)
			{
			}
			sqlData_t(eFieldTypes type, const wxString& fieldRefType, const wxString& fieldRefName) : m_type(type), m_field(fieldRefType, fieldRefName)
			{
			}
			sqlData_t(const sqlData_t& rhs) : m_type(rhs.m_type)
			{
				if (rhs.m_type != eFieldTypes::eFieldTypes_Reference) {
					m_field.m_fieldName = rhs.m_field.m_fieldName;
				}
				else {
					m_field.m_fieldRefName.m_fieldRefType = rhs.m_field.m_fieldRefName.m_fieldRefType;
					m_field.m_fieldRefName.m_fieldRefName = rhs.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
			sqlData_t& operator=(const sqlData_t& rhs) {
				m_type = rhs.m_type;
				if (rhs.m_type != eFieldTypes::eFieldTypes_Reference) {
					m_field.m_fieldName = rhs.m_field.m_fieldName;
				}
				else {
					m_field.m_fieldRefName.m_fieldRefType = rhs.m_field.m_fieldRefName.m_fieldRefType;
					m_field.m_fieldRefName.m_fieldRefName = rhs.m_field.m_fieldRefName.m_fieldRefName;
				}
				return *this;
			}
			~sqlData_t() {}
		};

		std::vector< sqlData_t> m_types;

		sqlField_t(const wxString& fieldTypeName) : m_fieldTypeName(fieldTypeName) {
		}

		void AppendType(eFieldTypes type) {
			m_types.emplace_back(type);
		}

		void AppendType(eFieldTypes type, const wxString& fieldName) {
			m_types.emplace_back(type, fieldName);
		}

		void AppendType(eFieldTypes type, const wxString& fieldRefType, const wxString& fieldRefName) {
			m_types.emplace_back(type, fieldRefType, fieldRefName);
		}
	};

	//get special filed data
	static unsigned int GetSQLFieldCount(IMetaAttributeObject* metaAttr);
	static wxString GetSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& aggr = wxEmptyString);
	static wxString GetCompositeSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& oper = wxT("="));

	//get data sql
	static sqlField_t GetSQLFieldData(IMetaAttributeObject* metaAttr);

	//process default query
	static int ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);

	//set value attribute 
	static void SetValueAttribute(IMetaAttributeObject* metaAttribute, const CValue& cValue, PreparedStatement* statement, int& position);

	//get value from attribute
	static CValue GetValueAttribute(const wxString& fieldName, const eFieldTypes& fldType, IMetaAttributeObject* metaAttr, DatabaseResultSet* resultSet, bool createData = true);
	static CValue GetValueAttribute(const wxString& fieldName, IMetaAttributeObject* metaAttribute, DatabaseResultSet* resultSet, bool createData = true);
	static CValue GetValueAttribute(IMetaAttributeObject* metaAttribute, DatabaseResultSet* resultSet, bool createData = true);

	//contain meta type
	bool ContainMetaType(enum eMetaObjectType type) const;

	//ctor 
	IMetaAttributeObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString) :
		IMetaObject(name, synonym, comment), IAttributeInfo(eValueTypes::TYPE_STRING), m_fillCheck(false)
	{
	}

	IMetaAttributeObject(const eValueTypes& valType) :
		IMetaObject(), IAttributeInfo(valType), m_fillCheck(false)
	{
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return m_parent->GetSelectorDataType();
	}

	//Create value by selected type
	virtual CValue CreateValue() const;
	virtual CValue* CreateValueRef() const;

	//get metadata
	virtual IMetadata* GetMetadata() const {
		return m_metaData;
	}

	virtual wxString GetFieldNameDB() const {
		return wxString::Format("fld%i", m_metaId);
	}

	//get sql type for db 
	virtual wxString GetSQLTypeObject(const CLASS_ID& clsid) const;

	//check if attribute is default 
	virtual bool DefaultAttribute() const = 0;

	//check if attribute is fill 
	virtual bool FillCheck() const {
		return m_fillCheck &&
			GetTypeCount() > 0;
	}

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnDeleteMetaObject();

protected:

	OptionList* GetDateTimeFormat(Property*) {
		OptionList* optList = new OptionList;
		optList->AddOption(_("date"), eDateFractions::eDateFractions_Date);
		optList->AddOption(_("date and time"), eDateFractions::eDateFractions_DateTime);
		optList->AddOption(_("time"), eDateFractions::eDateFractions_Time);
		return optList;
	}

protected:

	bool m_fillCheck;
	CValue m_defValue;

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class CMetaAttributeObject : public IMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaAttributeObject);
public:

	CMetaAttributeObject(const eValueTypes& valType = eValueTypes::TYPE_STRING);

	//check if attribute is default 
	virtual bool DefaultAttribute() const {
		return false;
	}

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("attribute");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

class CMetaDefaultAttributeObject : public IMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaAttributeObject);
private:

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_BOOLEAN);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}
	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, CValueQualifierNumber& qNumber, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_NUMBER);
		IAttributeWrapper::SetNumber(qNumber.m_precision, qNumber.m_scale);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}
	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, CValueQualifierDate& qDate, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_DATE);
		IAttributeWrapper::SetDate(qDate.m_dateTime);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}
	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, CValueQualifierString& qString, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
		IAttributeWrapper::SetString(qString.m_length);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(clsid);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid, const metaDescription_t& descr, bool fillCheck, const CValue& defValue)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(clsid, descr);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::ClearAllMetatype();
		m_fillCheck = fillCheck;
	}

public:

	CMetaDefaultAttributeObject()
		: IMetaAttributeObject()
	{
		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}

	static CMetaDefaultAttributeObject* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck = false, const bool& defValue = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, fillCheck, defValue);
	}

	static CMetaDefaultAttributeObject* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck = false, const number_t& defValue = 0) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierNumber(precision, scale), fillCheck, defValue);
	}

	static CMetaDefaultAttributeObject* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		eDateFractions dateTime, bool fillCheck = false, const wxDateTime& defValue = wxLongLong(emptyDate)) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierDate(dateTime), fillCheck, defValue);
	}

	static CMetaDefaultAttributeObject* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, bool fillCheck = false, const wxString& defValue = wxEmptyString) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierString(length), fillCheck, defValue);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static CMetaDefaultAttributeObject* CreateEmptyType(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, fillCheck);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static CMetaDefaultAttributeObject* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid,
		bool fillCheck = false, const CValue& defValue = CValue()) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, clsid, fillCheck, defValue);
	}

	static CMetaDefaultAttributeObject* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid, const metaDescription_t& descr,
		bool fillCheck = false, const CValue& defValue = CValue()) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, clsid, descr, fillCheck, defValue);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("defaultAttribute");
	}
	//check if attribute is default 
	virtual bool DefaultAttribute() const {
		return true;
	}
};

#endif