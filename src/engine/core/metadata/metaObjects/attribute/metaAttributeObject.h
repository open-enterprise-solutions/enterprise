#ifndef _ATTRIBUTES_H__
#define _ATTRIBUTES_H__

#include "metadata/metaObjects/metaObject.h"
#include "common/attributeInfo.h"

enum eSelectMode {
	eSelectMode_Items = 1,
	eSelectMode_Folders,
	eSelectMode_FoldersAndItems
};

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
		eFieldTypes_Null,
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
	static unsigned short GetSQLFieldCount(IMetaAttributeObject* metaAttr);
	static wxString GetSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& aggr = wxEmptyString);
	static wxString GetCompositeSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& oper = wxT("="));

	//get data sql
	static sqlField_t GetSQLFieldData(IMetaAttributeObject* metaAttr);

	//process default query
	static int ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);

	//set value attribute 
	static void SetValueAttribute(IMetaAttributeObject* metaAttribute, const CValue& cValue, class PreparedStatement* statement, int& position);

	//get value from attribute
	static CValue GetValueAttribute(const wxString& fieldName, const eFieldTypes& fldType, IMetaAttributeObject* metaAttr, class DatabaseResultSet* resultSet, bool createData = true);
	static CValue GetValueAttribute(const wxString& fieldName, IMetaAttributeObject* metaAttribute, class DatabaseResultSet* resultSet, bool createData = true);
	static CValue GetValueAttribute(IMetaAttributeObject* metaAttribute, class DatabaseResultSet* resultSet, bool createData = true);

	template <class retType>
	static retType GetValueAttributeAndConvert(IMetaAttributeObject* metaAttribute, class DatabaseResultSet* resultSet, bool createData = true) {
		return GetValueAttribute(metaAttribute, resultSet, createData).ConvertToValue<retType>();
	}

	eSelectMode GetSelectMode() const {
		return m_selMode;
	}

	//contain meta type
	bool ContainMetaType(enum eMetaObjectType type) const;

	//ctor 
	IMetaAttributeObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString) :
		IMetaObject(name, synonym, comment), IAttributeInfo(eValueTypes::TYPE_STRING), m_selMode(eSelectMode::eSelectMode_Items)
	{
	}

	IMetaAttributeObject(const eValueTypes& valType) :
		IMetaObject(), IAttributeInfo(valType), m_selMode(eSelectMode::eSelectMode_Items)
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
		return wxString::Format(wxT("fld%i"), m_metaId);
	}

	//get sql type for db 
	virtual wxString GetSQLTypeObject(const CLASS_ID& clsid) const;

	//check if attribute is default 
	virtual bool DefaultAttribute() const = 0;

	//check if attribute is fill 
	virtual bool FillCheck() const = 0;

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnDeleteMetaObject();

protected:

	CValue m_defValue;
	eSelectMode m_selMode;
};

class CMetaAttributeObject : public IMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaAttributeObject);
protected:
	PropertyCategory* m_categoryType = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertyType = IPropertyObject::CreateTypeProperty(m_categoryType, "type");
	PropertyCategory* m_categoryAttribute = IPropertyObject::CreatePropertyCategory("attribute");
	Property* m_propertyFillCheck = IPropertyObject::CreateProperty(m_categoryAttribute, { "fill_check", "fill check" }, PropertyType::PT_BOOL);
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

	//check if attribute is fill 
	virtual bool FillCheck() const {
		return m_propertyFillCheck->GetValueAsBoolean() &&
			GetClsidCount() > 0;
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
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
		const CLASS_ID& clsid, bool fillCheck, const CValue& defValue, eSelectMode selMode)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(clsid);
		m_fillCheck = fillCheck; m_defValue = defValue; m_selMode = selMode;
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid, const metaDescription_t& descr, bool fillCheck, const CValue& defValue, eSelectMode selMode)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::SetDefaultMetatype(clsid, descr);
		m_fillCheck = fillCheck; m_defValue = defValue; m_selMode = selMode;
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::ClearAllMetatype();
		m_fillCheck = fillCheck; 
	}

	CMetaDefaultAttributeObject(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, eSelectMode selMode)
		: IMetaAttributeObject(name, synonym, comment)
	{
		IAttributeWrapper::ClearAllMetatype();
		m_fillCheck = fillCheck; m_selMode = selMode;
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
		bool fillCheck = false, eSelectMode selMode = eSelectMode::eSelectMode_Items) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, fillCheck, selMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static CMetaDefaultAttributeObject* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid,
		bool fillCheck = false, const CValue& defValue = CValue(), eSelectMode selMode = eSelectMode::eSelectMode_Items) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, clsid, fillCheck, defValue, selMode);
	}

	static CMetaDefaultAttributeObject* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const CLASS_ID& clsid, const metaDescription_t& descr,
		bool fillCheck = false, const CValue& defValue = CValue(), eSelectMode selMode = eSelectMode::eSelectMode_Items) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, clsid, descr, fillCheck, defValue, selMode);
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

	//check if attribute is fill 
	virtual bool FillCheck() const {
		return m_fillCheck &&
			GetClsidCount() > 0;
	}

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

private:

	bool m_fillCheck;
};

#endif