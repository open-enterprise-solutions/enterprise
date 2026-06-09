#ifndef __ATTRIBUTE_OBJECT_H__
#define __ATTRIBUTE_OBJECT_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/backend_type.h"
#include "backend/objCtorDefs.h"
#include "backend/query/queryColumn.h"   // ibBackendQueryColumn — an attribute IS a query column

#include "metaAttributeObjectEnum.h"

class ibDatabaseResultSet;   // L1 cursor — only named as a pointer in GetBinaryData below
class ibPreparedStatement;   // L1 prepared statement — only named as a pointer in SetBinaryData below

class BACKEND_API ibValueMetaObjectAttributeBase :
	public ibValueMetaObject, public ibBackendTypeConfigFactory, public ibBackendQueryColumn {
	public:

	enum ibFieldTypes {
		ibFieldTypes_Empty = 0,
		ibFieldTypes_Boolean,
		ibFieldTypes_Number,
		ibFieldTypes_Date,
		ibFieldTypes_String,
		ibFieldTypes_Null,
		ibFieldTypes_Enum,
		ibFieldTypes_Reference,
	};

	struct ibSQLField {

		wxString m_fieldTypeName;
		struct ibSQLData {
			ibFieldTypes m_type;
			struct ibData {
				wxString m_fieldName;
				struct ibRefData {
					wxString m_fieldRefType;
					wxString m_fieldRefName;
					ibRefData() {
					}
					ibRefData(const wxString& fieldRefType, const wxString& fieldRefName)
						: m_fieldRefType(fieldRefType), m_fieldRefName(fieldRefName) {
					}
					~ibRefData() {
					}
				} m_fieldRefName;

				ibData()
					: m_fieldName(wxEmptyString)
				{
				}

				ibData(const wxString& fieldName)
					: m_fieldName(fieldName) {
				}

				ibData(const wxString& fieldRefType, const wxString& fieldRefNam)
					: m_fieldRefName(fieldRefType, fieldRefNam) {
				}

				~ibData() {
				}

			} m_field;

			ibSQLData() : m_type(ibFieldTypes::ibFieldTypes_Empty)
			{
			}
			ibSQLData(ibFieldTypes type) : m_type(type)
			{
			}
			ibSQLData(ibFieldTypes type, const wxString& fieldName) : m_type(type), m_field(fieldName)
			{
			}
			ibSQLData(ibFieldTypes type, const wxString& fieldRefType, const wxString& fieldRefName) : m_type(type), m_field(fieldRefType, fieldRefName)
			{
			}
			ibSQLData(const ibSQLData& rhs) : m_type(rhs.m_type)
			{
				if (rhs.m_type != ibFieldTypes::ibFieldTypes_Reference) {
					m_field.m_fieldName = rhs.m_field.m_fieldName;
				}
				else {
					m_field.m_fieldRefName.m_fieldRefType = rhs.m_field.m_fieldRefName.m_fieldRefType;
					m_field.m_fieldRefName.m_fieldRefName = rhs.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
			ibSQLData& operator=(const ibSQLData& rhs) {
				m_type = rhs.m_type;
				if (rhs.m_type != ibFieldTypes::ibFieldTypes_Reference) {
					m_field.m_fieldName = rhs.m_field.m_fieldName;
				}
				else {
					m_field.m_fieldRefName.m_fieldRefType = rhs.m_field.m_fieldRefName.m_fieldRefType;
					m_field.m_fieldRefName.m_fieldRefName = rhs.m_field.m_fieldRefName.m_fieldRefName;
				}
				return *this;
			}
			~ibSQLData() {}
		};

		std::vector< ibSQLData> m_types;

		ibSQLField(const wxString& fieldTypeName) : m_fieldTypeName(fieldTypeName) {
		}

		void AppendType(ibFieldTypes type) {
			m_types.emplace_back(type);
		}

		void AppendType(ibFieldTypes type, const wxString& fieldName) {
			m_types.emplace_back(type, fieldName);
		}

		void AppendType(ibFieldTypes type, const wxString& fieldRefType, const wxString& fieldRefName) {
			m_types.emplace_back(type, fieldRefType, fieldRefName);
		}

		///////////////////////////////////////////////////////
		auto begin() { return m_types.begin(); }
		auto end() { return m_types.end(); }
		///////////////////////////////////////////////////////
	};

	//get special filed data
	static unsigned short GetSQLFieldCount(const ibValueMetaObjectAttributeBase* metaAttr);
	static wxString GetSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr, const wxString& aggr = wxEmptyString);
	static wxString GetCompositeSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr, const wxString& cmp = wxT("="));
	static wxString GetExcludeSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr);

	//get data sql
	static ibSQLField GetSQLFieldData(const ibValueMetaObjectAttributeBase* metaAttr);

	//process default query
	static int ProcessAttribute(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr);

	// (Value assembly from / binding to a DB row moved to ibDbTableProvider::GetValueAttribute /
	// ::SetValueAttribute — it is a DB provider concern, not the metadata attribute's. See
	// query/dbTableProvider.h.)

	//store value 
	static void SetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, const ibReaderMemory& reader, ibPreparedStatement* statement,
		int& position);
	static void SetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, const ibReaderMemory& reader, ibPreparedStatement* statement);
	static void GetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, ibWriterMemory& writer, ibDatabaseResultSet* resultSet);

	//contain type
	bool ContainType(const ibValueTypes& valType) const;
	bool ContainType(const ibClassID& clsid) const;

	//contain meta type
	bool ContainMetaType(ibCtorObjectMetaType type) const;

	//equal type 
	bool EqualType(const ibClassID& clsid, const ibTypeDescription& rhs) const;

	//ctor 
	ibValueMetaObjectAttributeBase(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString) :
		ibValueMetaObject(name, synonym, comment)
	{
	}

#pragma region value_factory 

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const;

	//Create value by selected type
	virtual ibValue CreateValue() const;
	virtual ibValue* CreateValueRef() const;

#pragma endregion

	virtual wxString GetFieldNameDB() const { return wxString::Format(wxT("fld%i"), m_metaId); }

	// --- ibBackendQueryColumn: an attribute IS a query column ------------
	// GetTypeDesc() is the column's typed accessor — but it is ALSO declared by the
	// other base (ibBackendTypeFactory). Re-declaring it here as one pure virtual
	// makes a single overrider for BOTH bases and resolves the otherwise-ambiguous
	// name (C2385); each concrete attribute supplies the body, reused as-is.
	// GetName likewise resolves the ambiguity between ibValueMetaObject::GetName and
	// the column's.
	virtual ibTypeDescription& GetTypeDesc() const override = 0;
	virtual wxString GetName() const override         { return ibValueMetaObject::GetName(); }
	virtual wxString GetPhysicalName() const override { return GetFieldNameDB(); }
	// The column's model/read id — for a DB attribute it IS the metaID (RAM tables key
	// their rows by it; the DB path keys its fields off the same id via GetFieldNameDB).
	virtual ibMetaID GetModelID() const override      { return GetMetaID(); }

	// Authoritative physical-field list — the attribute's OWN field machinery, so the DB
	// IR builder (sort / group-by) reads it straight off the column with no ResolveAttribute,
	// byte-identical to the former GetSQLFieldData path it replaced.
	virtual std::vector<wxString> GetSQLFields() const override {
		std::vector<wxString> out;
		for (auto& field : GetSQLFieldData(this))
			out.push_back(field.m_type == ibFieldTypes_Reference
			              ? field.m_field.m_fieldRefName.m_fieldRefName
			              : field.m_field.m_fieldName);
		return out;
	}

	//get sql type for db
	virtual wxString GetSQLTypeObject(const ibClassID& clsid) const;

	//check if attribute is fill 
	virtual bool FillCheck() const = 0;

	virtual ibItemMode GetItemMode() const { return ibItemMode::ibItemMode_Item; }
	virtual ibSelectMode GetSelectMode() const { return ibSelectMode::ibSelectMode_Items; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const { return m_metaData; }
	virtual ibMetaData* GetMetaData() { return m_metaData; }

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	// //after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

protected:
	ibValue m_defValue;
};

class BACKEND_API ibValueMetaObjectAttribute : public ibValueMetaObjectAttributeBase {
	public:

	ibValueMetaObjectAttribute(const ibValueTypes& valType = ibValueTypes::TYPE_STRING) :
		ibValueMetaObjectAttributeBase()
	{
		m_propertyType->SetValue(ibValue::GetIDByVT(valType));
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//check if attribute is fill 
	virtual bool FillCheck() const { return m_propertyFillCheck->GetValueAsBoolean() && GetClsidCount() > 0; }

	virtual ibItemMode GetItemMode() const;
	virtual ibSelectMode GetSelectMode() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertyType->GetValueAsTypeDesc(); }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	ibPropertyCategory* m_categoryType = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryType, wxT("Type"), _("Type"), ibValueTypes::TYPE_STRING);
	ibPropertyCategory* m_categoryAttribute = ibPropertyObject::CreatePropertyCategory(wxT("Attribute"), _("Attribute"));
	ibPropertyBoolean* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryAttribute, wxT("FillCheck"), _("Fill check"));
	ibPropertyCategory* m_categoryPresentation = ibPropertyObject::CreatePropertyCategory(wxT("Presentation"), _("Presentation"));
	ibPropertyEnum<ibValueEnumSelectMode>* m_propertySelectMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSelectMode>>(m_categoryPresentation, wxT("Select"), _("Select group and items"), ibSelectMode::ibSelectMode_Items);
	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("Group"), _("Group"));
	ibPropertyEnum<ibValueEnumItemMode>* m_propertyItemMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumItemMode>>(m_categoryGroup, wxT("ItemMode"), _("Item mode"), ibItemMode::ibItemMode_Item);
};

class BACKEND_API ibValueMetaObjectAttributePredefined : public ibValueMetaObjectAttributeBase {
	public:
private:

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierNumber& qNumber, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);
		m_typeDesc.SetNumber(qNumber.m_precision, qNumber.m_scale);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierDate& qDate, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_DATE);
		m_typeDesc.SetDate(qDate.m_dateTime);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierString& qString, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		m_typeDesc.SetString(qString.m_length);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(clsid);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(clsid, descr);
		m_fillCheck = fillCheck; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, ibItemMode itemMode, ibSelectMode selectMode)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.ClearMetaType();
		m_fillCheck = fillCheck;
	}

public:

	ibValueMetaObjectAttributePredefined()
		: ibValueMetaObjectAttributeBase(), m_itemMode(ibItemMode::ibItemMode_Item), m_selectMode(ibSelectMode::ibSelectMode_Items) {
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// synonym translate
	virtual wxString GetSynonym() const { return m_strSynonym; }
	virtual void SetSynonym(const wxString& strSynonym) {}

	//check if attribute is fill 
	virtual bool FillCheck() const { return m_fillCheck && m_typeDesc.GetClsidCount() > 0; }
	virtual ibItemMode GetItemMode() const { return m_itemMode; }
	virtual ibSelectMode GetSelectMode() const { return m_selectMode; }

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_typeDesc; }

	friend class ibValue;

protected:

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	mutable ibTypeDescription m_typeDesc;

	bool m_fillCheck;
	ibItemMode m_itemMode;
	ibSelectMode m_selectMode;

	wxString m_strSynonym;
};

#endif