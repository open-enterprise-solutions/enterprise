#ifndef __ATTRIBUTE_OBJECT_H__
#define __ATTRIBUTE_OBJECT_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/backend_type.h"
#include "backend/objCtorDefs.h"
#include "backend/query/queryColumn.h"   // ibBackendQueryColumn — an attribute IS a query column

#include "metaAttributeObjectEnum.h"

class ibQueryResult;         // L2 cursor — GetBinaryData reads through it (dump)
class ibQueryStatement;      // L2 statement — SetBinaryData binds through it (restore); no raw L1 here
class ibStructureBatch;      // per-table DDL/seed batch — ProcessAttribute pours its column DDL into it

class BACKEND_API ibValueMetaObjectAttributeBase :
	public ibValueMetaObject, public ibBackendTypeConfigFactory, public ibBackendQueryColumn {
	public:

	// (The whole SQL-field façade — GetSQLFieldName / GetCompositeSQLFieldName / GetExcludeSQLFieldName /
	//  GetSQLFieldCount / GetSQLFieldData, plus the ibFieldTypes / ibSQLField re-exports — is GONE. An
	//  attribute is just an ibBackendQueryColumn; its physical fields come from the column-layout tier
	//  (columnLayout.h: ColumnFieldList / ColumnFieldNames / ColumnComparePredicate / ibColumnCodec).
	//  Register lowering uses the ibReg* helpers in registerQueryLowering.h.)

	// (Column DDL — the type-set diff — moved OFF the attribute to the structure tier's free function
	// DiffColumnInto(batch, srcCol, dstCol). An attribute is just a column; it does not own its DDL.)

	// (Value assembly from / binding to a DB row moved to ibDbTableProvider::GetValueAttribute /
	// ::SetValueAttribute — it is a DB provider concern, not the metadata attribute's. See
	// query/dbTableProvider.h. The binary dump/restore codec is ibDataMover::BinaryToStatement /
	// ::BinaryFromResult (L3-3) — callers use the tier directly, no attribute forwarder.)

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

	// --- ibBackendQueryColumn: an attribute IS a query column ------------
	// GetTypeDesc() is the column's typed accessor — but it is ALSO declared by the
	// other base (ibBackendTypeFactory). Re-declaring it here as one pure virtual
	// makes a single overrider for BOTH bases and resolves the otherwise-ambiguous
	// name (C2385); each concrete attribute supplies the body, reused as-is.
	// GetName likewise resolves the ambiguity between ibValueMetaObject::GetName and
	// the column's.
	virtual ibTypeDescription& GetTypeDesc() const override = 0;
	virtual wxString GetName() const override         { return ibValueMetaObject::GetName(); }
	virtual wxString GetPhysicalName() const override { return wxString::Format(wxT("fld%i"), m_metaId); }
	// The column's model/read id — for a DB attribute it IS the metaID (RAM tables key
	// their rows by it; the DB path keys its fields off the same id via GetPhysicalName).
	virtual ibMetaID GetColumnId() const override      { return GetMetaID(); }

	// Authoritative physical-field list — the data fields the DB IR builder (sort / group-by) reads
	// straight off the column: the per-type primitives + the reference's _RRRef, skipping the _TYPE
	// tag and the _RTRef. Defined in metaAttributeObjectQuery.cpp over the column-layout tier.
	virtual std::vector<wxString> GetValueFields() const override;

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