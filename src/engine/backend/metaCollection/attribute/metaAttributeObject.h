#ifndef _ATTRIBUTES_H__
#define _ATTRIBUTES_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/wrapper/typeInfo.h"

enum eSelectMode {
	eSelectMode_Items = 1,
	eSelectMode_Folders,
	eSelectMode_FoldersAndItems
};

enum eItemMode {
	eItemMode_Item,
	eItemMode_Folder,
	eItemMode_Folder_Item
};

class BACKEND_API IMetaObjectAttribute : public IMetaObject,
	public ITypeAttribute {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectAttribute);
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

		///////////////////////////////////////////////////////
		auto begin() { return m_types.begin(); }
		auto end() { return m_types.end(); }
		///////////////////////////////////////////////////////
	};

	//get special filed data
	static unsigned short GetSQLFieldCount(IMetaObjectAttribute* metaAttr);
	static wxString GetSQLFieldName(IMetaObjectAttribute* metaAttr, const wxString& aggr = wxEmptyString);
	static wxString GetCompositeSQLFieldName(IMetaObjectAttribute* metaAttr, const wxString& oper = wxT("="));
	static wxString GetExcluteSQLFieldName(IMetaObjectAttribute* metaAttr);

	//get data sql
	static sqlField_t GetSQLFieldData(IMetaObjectAttribute* metaAttr);

	//process default query
	static int ProcessAttribute(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr);

	//set value attribute 
	static void SetValueAttribute(IMetaObjectAttribute* metaAttribute, const CValue& cValue, class IPreparedStatement* statement, int& position);

	//get value from attribute
	static bool GetValueAttribute(const wxString& fieldName, const eFieldTypes& fldType, IMetaObjectAttribute* metaAttr, CValue& retValue, class IDatabaseResultSet* resultSet, bool createData = true);
	static bool GetValueAttribute(const wxString& fieldName, IMetaObjectAttribute* metaAttribute, CValue& retValue, class IDatabaseResultSet* resultSet, bool createData = true);
	static bool GetValueAttribute(IMetaObjectAttribute* metaAttribute, CValue& retValue, class IDatabaseResultSet* resultSet, bool createData = true);

	//contain meta type
	bool ContainMetaType(enum eCtorMetaType type) const;

	//ctor 
	IMetaObjectAttribute(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString) :
		IMetaObject(name, synonym, comment), ITypeAttribute(eValueTypes::TYPE_STRING)
	{
	}

	IMetaObjectAttribute(const eValueTypes& valType) :
		IMetaObject(), ITypeAttribute(valType)
	{
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return m_parent->GetSelectorDataType();
	}

	//Create value by selected type
	virtual CValue CreateValue() const;
	virtual CValue* CreateValueRef() const;

	//get metaData
	virtual IMetaData* GetMetaData() const {
		return m_metaData;
	}

	virtual wxString GetFieldNameDB() const {
		return wxString::Format(wxT("fld%i"), m_metaId);
	}

	//get sql type for db 
	virtual wxString GetSQLTypeObject(const class_identifier_t& clsid) const;

	//check if attribute is default 
	virtual bool DefaultAttribute() const = 0;

	//check if attribute is fill 
	virtual bool FillCheck() const = 0;

	virtual eItemMode GetItemMode() const {
		return eItemMode::eItemMode_Item;
	}

	virtual eSelectMode GetSelectMode() const {
		return eSelectMode::eSelectMode_Items;
	}

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	// //after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

protected:
	CValue m_defValue;
};

class BACKEND_API CMetaObjectAttribute : public IMetaObjectAttribute {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectAttribute);
private:
	OptionList* GetSelectMode(PropertyOption*) {
		OptionList* opt_list = new OptionList;
		opt_list->AddOption(_("items"), eSelectMode_Items);
		opt_list->AddOption(_("folders and items"), eSelectMode_FoldersAndItems);
		opt_list->AddOption(_("folders"), eSelectMode_Folders);
		return opt_list;
	}
	OptionList* GetItemMode(PropertyOption*) {
		OptionList* opt_list = new OptionList;
		opt_list->AddOption(_("for item"), eItemMode_Item);
		opt_list->AddOption(_("for folder"), eItemMode_Folder);
		opt_list->AddOption(_("for folder and item"), eItemMode_Folder_Item);
		return opt_list;
	}
protected:
	PropertyCategory* m_categoryType = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertyType = IPropertyObject::CreateTypeProperty(m_categoryType, "type");
	PropertyCategory* m_categoryAttribute = IPropertyObject::CreatePropertyCategory("attribute");
	Property* m_propertyFillCheck = IPropertyObject::CreateProperty(m_categoryAttribute, { "fill_check", "fill check" }, PropertyType::PT_BOOL);
	PropertyCategory* m_categoryPresentation = IPropertyObject::CreatePropertyCategory({ "presentation", _("presentation") });
	Property* m_propertySelectMode = IPropertyObject::CreateProperty(m_categoryPresentation, { "select",  "select group and items" }, &CMetaObjectAttribute::GetSelectMode, eSelectMode::eSelectMode_Items);
	PropertyCategory* m_categoryGroup = IPropertyObject::CreatePropertyCategory({ "group", _("group") });
	Property* m_propertyItemMode = IPropertyObject::CreateProperty(m_categoryGroup, { "use",  "use" }, &CMetaObjectAttribute::GetItemMode, eItemMode::eItemMode_Item);
public:

	CMetaObjectAttribute(const eValueTypes& valType = eValueTypes::TYPE_STRING);

	//check if attribute is default 
	virtual bool DefaultAttribute() const {
		return false;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//check if attribute is fill 
	virtual bool FillCheck() const {
		return m_propertyFillCheck->GetValueAsBoolean() &&
			GetClsidCount() > 0;
	}

	virtual eItemMode GetItemMode() const;
	virtual eSelectMode GetSelectMode() const;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class BACKEND_API CMetaObjectAttributeDefault : public IMetaObjectAttribute {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectAttribute);
private:

	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_BOOLEAN);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}
	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment, const qualifierNumber_t& qNumber, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_NUMBER);
		ITypeWrapper::SetNumber(qNumber.m_precision, qNumber.m_scale);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}
	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment, const qualifierDate_t& qDate, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_DATE);
		ITypeWrapper::SetDate(qDate.m_dateTime);
		m_fillCheck = fillCheck; m_defValue = defValue; 
	}
	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment, const qualifierString_t& qString, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
		ITypeWrapper::SetString(qString.m_length);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(clsid);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr, bool fillCheck, const CValue& defValue, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::SetDefaultMetatype(clsid, descr);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	CMetaObjectAttributeDefault(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, eItemMode itemMode, eSelectMode selectMode)
		: IMetaObjectAttribute(name, synonym, comment), m_itemMode(itemMode), m_selectMode(selectMode)
	{
		ITypeWrapper::ClearAllMetatype();
		m_fillCheck = fillCheck;
	}

public:

	CMetaObjectAttributeDefault()
		: IMetaObjectAttribute(), m_itemMode(eItemMode::eItemMode_Item), m_selectMode(eSelectMode::eSelectMode_Items) {
		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}

	static CMetaObjectAttributeDefault* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, false, eValueTypes::TYPE_BOOLEAN, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, fillCheck, eValueTypes::TYPE_BOOLEAN, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, const bool& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, fillCheck, CValue(defValue), useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierNumber_t(precision, scale), false, eValueTypes::TYPE_NUMBER, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierNumber_t(precision, scale), fillCheck, eValueTypes::TYPE_NUMBER, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, const number_t& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierNumber_t(precision, scale), fillCheck, CValue(defValue), useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		eDateFractions dateTime, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierDate_t(dateTime), false, eValueTypes::TYPE_DATE, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		eDateFractions dateTime, bool fillCheck, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierDate_t(dateTime), fillCheck, eValueTypes::TYPE_DATE, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		eDateFractions dateTime, bool fillCheck, const wxDateTime& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierDate_t(dateTime), fillCheck, CValue(defValue), useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierString_t(length), false, eValueTypes::TYPE_STRING, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, bool fillCheck, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierString_t(length), fillCheck, eValueTypes::TYPE_STRING, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, bool fillCheck, const wxString& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, qualifierString_t(length), fillCheck, CValue(defValue), useItem, selectMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static CMetaObjectAttributeDefault* CreateEmptyType(const wxString& name, const wxString& synonym, const wxString& comment,
		eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, false, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateEmptyType(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, fillCheck, useItem, selectMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static CMetaObjectAttributeDefault* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, clsid, false, CValue(), useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, const CValue& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, clsid, false, defValue, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, bool fillCheck, const CValue& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, clsid, fillCheck, defValue, useItem, selectMode);
	}

	static CMetaObjectAttributeDefault* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const class_identifier_t& clsid, const typeDescription_t::typeData_t& descr,
		bool fillCheck, const CValue& defValue, eItemMode useItem = eItemMode::eItemMode_Item, eSelectMode selectMode = eSelectMode::eSelectMode_Items) {
		return CValue::CreateAndConvertObjectValueRef<CMetaObjectAttributeDefault>(name, synonym, comment, clsid, descr, fillCheck, defValue, useItem, selectMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//check if attribute is default 
	virtual bool DefaultAttribute() const {
		return true;
	}

	//check if attribute is fill 
	virtual bool FillCheck() const {
		return m_fillCheck &&
			GetClsidCount() > 0;
	}

	virtual eItemMode GetItemMode() const {
		return m_itemMode;
	}

	virtual eSelectMode GetSelectMode() const {
		return m_selectMode;
	}

	friend class CValue;

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

private:
	bool m_fillCheck;
	eItemMode m_itemMode;
	eSelectMode m_selectMode;
};

#endif