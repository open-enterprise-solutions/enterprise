#ifndef __ATTRIBUTE_OBJECT_H__
#define __ATTRIBUTE_OBJECT_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/backend_type.h"
#include "backend/objCtorDefs.h"
#include "backend/query/queryColumn.h"   // ibBackendSourceColumn (the attribute's own face) + ibBackendQueryColumn (the face it HOLDS)

#include <memory>   // the query face is held, not inherited — see ibMetaAttributeColumn below

#include "metaAttributeObjectEnum.h"

class ibQueryResult;         // L2 cursor — GetBinaryData reads through it (dump)
class ibQueryStatement;      // L2 statement — SetBinaryData binds through it (restore); no raw L1 here
class ibStructureBatch;      // per-table DDL/seed batch — ProcessAttribute pours its column DDL into it

// ⭐⭐ AN ATTRIBUTE IS A DESCRIPTIVE COLUMN AND *HOLDS* A QUERY ONE.
//
// It stays an ibBackendSourceColumn — a name, a synonym, a type, an icon — because that is what the
// form binding walks to (ibBackendTypeSourceFactory::WalkSource returns exactly this) and what the
// composer's own metaobject already models the same way. What it no longer IS is a QUERY column.
//
// The reason is ownership, and it is written down once in docs/ownership-authority.md: this object
// lives under the runtime's own reference count (ibValueMetaObject -> ibValue, whose DecrRef does
// `delete this` at zero), while the query tier holds columns by std::shared_ptr, whose count lives
// in a control block outside the object. Neither count can see the other and BOTH delete. Fused by
// inheritance the two do not touch, they overlap — and an overlap has no safe outcome. Held as a
// member they have exactly one point of contact: when the attribute dies, its destructor destroys
// the member, which is one `-1` in the control block. Nobody sweeps and nobody is notified.
class BACKEND_API ibValueMetaObjectAttributeBase :
	public ibValueMetaObject, public ibBackendTypeConfigFactory, public ibBackendSourceColumn {
	public:

	// ⭐ THE QUERY FACE OF THIS ATTRIBUTE — and nothing else. It IS an ibBackendQueryColumn, so the
	// query tiers meet what they always met; they are not touched by any of this. It STORES no
	// answer of its own: every one is read through the way back, because two objects each holding a
	// name and a type would be two truths about one thing, disagreeing the first time the attribute
	// is renamed.
	//
	// The way back is NON-OWNING and the attribute CLEARS it as it dies. An attribute lives as long
	// as the metadata does, so for every ordinary read this pointer is exactly as good as the
	// attribute's own address; but a query RESULT may outlive the configuration it was read from,
	// and it holds this facade by shared_ptr. Detached, the facade answers as a column with nothing
	// behind it rather than reading freed memory. It must never keep the attribute alive: that would
	// put the runtime's count under the shared_ptr's, one indirection away from the very mixture
	// this arrangement exists to prevent.
	class BACKEND_API ibMetaAttributeColumn : public ibBackendQueryColumn {
	public:
		explicit ibMetaAttributeColumn(const ibValueMetaObjectAttributeBase* owner) : m_owner(owner) {}

		// Said by the owner, in its destructor — the only one who knows it is going.
		void Detach() { m_owner = nullptr; }
		const ibValueMetaObjectAttributeBase* Owner() const { return m_owner; }

		wxString GetName() const override         { return m_owner != nullptr ? m_owner->GetName() : wxString(); }
		wxString GetSynonym() const override      { return m_owner != nullptr ? m_owner->GetSynonym() : wxString(); }
		wxString GetComment() const override      { return m_owner != nullptr ? m_owner->GetComment() : wxString(); }
		bool     IsAllowed() const override       { return m_owner != nullptr && m_owner->IsAllowed(); }
		wxIcon   GetColumnIcon() const override   { return m_owner != nullptr ? m_owner->GetColumnIcon() : wxIcon(); }
		wxString GetPhysicalName() const override { return m_owner != nullptr ? m_owner->GetPhysicalName() : wxString(); }
		ibMetaID GetColumnId() const override     { return m_owner != nullptr ? m_owner->GetColumnId() : 0; }
		// ⚠ A DETACHED COLUMN HAS NO TYPE, and the interface returns a REFERENCE — so it answers with
		// a shared empty description rather than with a dangling one. Nothing may write through it,
		// which is already true of every type description a column hands out.
		ibTypeDescription& GetTypeDesc() const override;
		ibTypeDescription& GetTypeValueDesc() const override;

	private:
		const ibValueMetaObjectAttributeBase* m_owner;   // NON-OWNING; cleared on the owner's death
	};

	// The face to hand to anything that reads, joins or projects. Never `this` — see above.
	const ibBackendQueryColumn* GetQueryColumn() const { return m_column.get(); }
	// …and the same face as a co-owned handle, for a result that outlives the query that made it.
	std::shared_ptr<ibBackendQueryColumn> ShareQueryColumn() const { return m_column; }

	~ibValueMetaObjectAttributeBase() override {
		if (m_column) m_column->Detach();   // the whole interaction between the two ownerships
	}

	// A METAOBJECT COLUMN WEARS ITS OWN PICTURE. The column face asks (queryColumn.h), the
	// metaobject answers with the icon its class registered — so a dimension, a resource and a
	// plain attribute are told apart by whoever draws them, and neither the drawer nor this class
	// holds a list of kinds: each level already overrides GetIcon() for its own tree.
	wxIcon GetColumnIcon() const override { return GetIcon(); }

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

	// A declaration that IS a characteristic answers with the chart's own list (see the factory's
	// declaration in backend_type.h). Body in metaAttributeObject.cpp — the chart type is incomplete
	// here.
	virtual ibTypeDescription& GetTypeValueDesc() const override;

	// (IsEmptyTypeDesc lives on ibBackendTypeConfigFactory's base — backend_type.h — because that is
	//  where the type description itself is declared, and therefore the only place that can answer
	//  for every holder of one rather than for attributes alone.)

	virtual wxString GetName() const override         { return ibValueMetaObject::GetName(); }
	// GetSynonym is now ALSO declared by ibBackendSourceColumn (the column base) — same single-
	// overrider trick as GetName: the column synonym IS the metaobject synonym (the UI caption).
	virtual wxString GetSynonym() const override      { return ibValueMetaObject::GetSynonym(); }
	// …and GetComment the same way. It was ambiguous all along (ibValueMetaObject declares one and
	// ibBackendAbstractColumn declares another); nothing had called it THROUGH the attribute until
	// the query facade began forwarding to it, and an ambiguity nobody exercises is silent.
	virtual wxString GetComment() const override      { return ibValueMetaObject::GetComment(); }
	// IsAllowed (column base) routes to the metaobject's (IsEnabled && !IsDeleted) — so the source
	// explorer skips deleted / disabled fields without touching the metaobject.
	virtual bool IsAllowed() const override           { return ibValueMetaObject::IsAllowed(); }
	// ⚠ NOT `override` ANY MORE, and deliberately still HERE. These two are the query face's
	// questions, but the schema tier asks them of the ATTRIBUTE directly (an index name, a column
	// being renamed) and it is right to: both are derived from the metaID, which is the attribute's
	// own. The facade forwards to them, so there is still exactly one answer.
	virtual wxString GetPhysicalName() const { return wxString::Format(wxT("fld%i"), m_metaId); }
	// The column's model/read id — for a DB attribute it IS the metaID (RAM tables key
	// their rows by it; the DB path keys its fields off the same id via GetPhysicalName).
	virtual ibMetaID GetColumnId() const      { return GetMetaID(); }

	// (No GetValueFields here — the attribute is just a column; its value-field split is the tier free
	//  function ColumnValueFields(col) over DescribeColumnLayout, metadata-free, asked by the provider.)

	//check if attribute is fill
	virtual bool FillCheck() const = 0;

	virtual ibItemMode GetItemMode() const { return ibItemMode::ibItemMode_Item; }
	virtual ibSelectMode GetSelectMode() const { return ibSelectMode::ibSelectMode_Items; }
	virtual ibIndexingMode GetIndexingMode() const { return ibIndexingMode::ibIndexingMode_DontIndex; }

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

private:
	// ⭐ MADE ONCE, HERE, SO EVERY CONSTRUCTOR GETS IT — there are eight of them across this family
	// and none has to remember. Its life is exactly this attribute's, and this attribute's is the
	// metadata's; a result that outlives the query keeps it alive on its own account.
	std::shared_ptr<ibMetaAttributeColumn> m_column = std::make_shared<ibMetaAttributeColumn>(this);
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
	virtual ibIndexingMode GetIndexingMode() const { return m_propertyIndexingMode->GetValueAsEnum(); }

	//get type description
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertyType->GetValueAsTypeDesc(); }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh() override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:


	// per-type values: FillCheck/ItemMode/Select readable, Type (composite) binary
	// for now → becomes a Child sub-node later. Separate save/load (const on read).
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryType = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryType, wxT("Type"), _("Type"), ibValueTypes::TYPE_STRING);
	ibPropertyCategory* m_categoryAttribute = ibPropertyObject::CreatePropertyCategory(wxT("Attribute"), _("Attribute"));
	ibPropertyBoolean* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryAttribute, wxT("FillCheck"), _("Fill check"));
	ibPropertyEnum<ibValueEnumIndexingMode>* m_propertyIndexingMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumIndexingMode>>(m_categoryAttribute, wxT("Indexing"), _("Indexing"), ibIndexingMode::ibIndexingMode_DontIndex);
	ibPropertyCategory* m_categoryPresentation = ibPropertyObject::CreatePropertyCategory(wxT("Presentation"), _("Presentation"));
	ibPropertyEnum<ibValueEnumSelectMode>* m_propertySelectMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSelectMode>>(m_categoryPresentation, wxT("Select"), _("Select group and items"), ibSelectMode::ibSelectMode_Items);
	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("Group"), _("Group"));
	ibPropertyEnum<ibValueEnumItemMode>* m_propertyItemMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumItemMode>>(m_categoryGroup, wxT("ItemMode"), _("Item mode"), ibItemMode::ibItemMode_Item);
};

class BACKEND_API ibValueMetaObjectAttributePredefined : public ibValueMetaObjectAttributeBase {
	public:

	// (NOT SHOWN under its owner, and it needs no flag saying so: no owner ACCEPTS this
	// clsid as a child — ResolveChild lists attribute, tabular section, form, template,
	// command — so FilterChild already answers no. A predefined attribute is part of the
	// metatype's definition, not of the configuration.)
private:

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_BOOLEAN);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierNumber& qNumber, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);
		m_typeDesc.SetNumber(qNumber.m_precision, qNumber.m_scale);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierDate& qDate, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_DATE);
		m_typeDesc.SetDate(qDate.m_dateTime);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, const ibQualifierString& qString, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		m_typeDesc.SetString(qString.m_length);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(clsid);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr, bool fillCheck, const ibValue& defValue, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.SetDefaultMetaType(clsid, descr);
		m_fillCheck = fillCheck; m_indexingMode = indexingMode; m_defValue = defValue;
	}

	ibValueMetaObjectAttributePredefined(const wxString& name, const wxString& synonym, const wxString& comment, bool fillCheck, ibItemMode itemMode, ibSelectMode selectMode, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex)
		: ibValueMetaObjectAttributeBase(name, wxT(""), comment), m_itemMode(itemMode), m_selectMode(selectMode), m_strSynonym(synonym)
	{
		m_typeDesc.ClearMetaType();
		m_fillCheck = fillCheck; m_indexingMode = indexingMode;
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
	virtual ibIndexingMode GetIndexingMode() const { return m_indexingMode; }

	//get type description
	virtual ibTypeDescription& GetTypeDesc() const { return m_typeDesc; }


	// A predefined attribute is part of the metatype's definition, so its shape is fixed by the
	// constructor — with ONE exception. What a Parent field accepts follows the hierarchy the
	// OWNER declares (folders, or items subordinated to items), and that is a property the user
	// sets. The owner restates it whenever the declaration changes; nobody else may.
	void SetSelectMode(ibSelectMode selectMode) { m_selectMode = selectMode; }

	// The SECOND such exception, and for the same reason. A predefined attribute whose meaning
	// depends on a setting of its owner — an accounting register's debit account, which is `Account`
	// in a one-sided register and `AccountDr` beside `AccountCr` in a correspondence one — must
	// carry the caption that goes with the name it currently answers to, or the two say different
	// things about the same field. The owner restates both when the setting changes; SetSynonym
	// stays inert, so the object inspector still cannot edit what the metatype declares.
	void SetOwnerSynonym(const wxString& strSynonym) { m_strSynonym = strSynonym; }

	friend class ibValue;

protected:


	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	mutable ibTypeDescription m_typeDesc;

	bool m_fillCheck;
	ibItemMode m_itemMode;
	ibSelectMode m_selectMode;
	ibIndexingMode m_indexingMode = ibIndexingMode::ibIndexingMode_DontIndex;

	wxString m_strSynonym;
};

#endif