#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include "backend/metaCollection/partial/commonObject.h"

class BACKEND_API ibValueRecordDataObjectConstant;
class ibStructureBatch;   // per-table DDL/seed batch — ProcessAttribute pours the value column into it

// ibConstantQueryable — the L3 queryable for a constant (its single-row sys_const
// table). The constant no longer IS a queryable; it VENDS this adapter (a stable
// member), which forwards to the constant's own query methods. Nor is the constant a
// COLUMN any more: it HAS one (ibValueMetaObjectConstantColumn, nested below), and
// this queryable resolves its single column to it.
class ibValueMetaObjectConstant;
class BACKEND_API ibConstantQueryable : public ibBackendQueryable {
public:
	explicit ibConstantQueryable(const ibValueMetaObjectConstant* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // -> the constant's value column
	virtual wxString GetQueryTableName() const override;
	virtual const ibMetaData* GetMetaData() const override;                      // metadata context for column-based value reads
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;   // = m_meta — the guid / metaID are read off it
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;   // { RECORD_KEY } — the single-row UPSERT match
private:
	const ibValueMetaObjectConstant* m_meta;
};

class BACKEND_API ibValueMetaObjectConstant :
	public ibValueMetaObjectGenericData, public ibBackendTypeConfigFactory, public ibBackendQueryableHolder {
	public:

	// A constant belongs in a section too — it is a thing a person opens and edits.
	virtual bool IsInterfaceAllowed() const override { return true; }

	// ============================================================================
	// The constant's VALUE COLUMN — the physical half of a constant, and nothing else.
	//
	// A constant lives as ONE COLUMN of the shared single-row sys_const, and that used to be said by
	// making the constant itself an attribute. It made the constant a column and an object at once,
	// and it cannot be both: every form asks its source for a CompositeData (srcObject.h), so the
	// object half was produced by CASTING the constant into a class it does not derive from. The
	// virtual calls that followed landed in a foreign vtable — which is why a fully privileged user
	// got a read-only form, and why the same cast could just as well have jumped anywhere.
	//
	// So the column moves INSIDE. It owns NOTHING: name, id and type all come from the constant, so
	// the column the schema differ sees is the same column it saw before, merely described by a
	// different object. That is what keeps sys_const unchanged — no rename, no re-add, no migration.
	//
	// The id delegation is not a detail. Columns are matched between the baseline and the target BY
	// ID (schemaSnapshot.h), so a column with an id of its own would read as "the old one vanished,
	// a new one appeared" — a DROP plus an ADD, which silently empties every constant in the base.
	// ============================================================================
	class BACKEND_API ibValueMetaObjectConstantColumn : public ibValueMetaObjectAttributeBase {
	public:
		explicit ibValueMetaObjectConstantColumn(ibValueMetaObjectConstant* owner = nullptr)
			: ibValueMetaObjectAttributeBase(), m_owner(owner) {
		}

		// `Constant.Rate.Value` — the COLUMN is called Value, not the constant's own name. A
		// constant is one row with one column, and naming that column after the table produced
		// `Constant1.Constant1`: a path that says the same word twice and names the thing you are
		// standing on rather than what you are reading off it. The table is the constant; the
		// column is its value. (Physical storage is untouched — the field is still `fld<metaID>`,
		// keyed on the constant's id, so nothing in the schema moves.)
		static const wxChar* ValueColumnName() { return wxT("Value"); }
		virtual wxString GetName() const { return ValueColumnName(); }

		// The SYNONYM has to be delegated explicitly, and the reason is a trap worth naming: the base
		// derives it from GetName(), but ibValueMetaObject::GetName() is NOT virtual — only the query
		// column's is. So the base reads its OWN empty name, generates an empty synonym from it, and
		// the form loses the field's caption while everything else keeps working.
		// THE CAPTION IS THE COLUMN'S OWN NAME — `Value`, untranslated. Handing back the constant's
		// synonym drew the field under `Constant1` as another `Constant1`: the tree said the same
		// word twice and nothing said what the node was. And `Value` is a SYSTEM name — the word the
		// query itself writes — so it is not run through the translation table: a caption that says
		// one thing while the text underneath says another is worse than an untranslated word.
		// (Delegated explicitly rather than left to the base: the base derives its synonym from
		// GetName(), and ibValueMetaObject::GetName() is not virtual — only the query column's is —
		// so the base would read its own empty name and the caption would vanish.)
		virtual wxString GetSynonym() const override { return ValueColumnName(); }

		// `fld<metaID>` — the SAME rule the attribute base applied when the constant itself was the
		// column, keyed on the CONSTANT's id. This is the second half of "sys_const does not move":
		// the id decides which column the differ matches, the name decides which field it renders.
		virtual wxString GetPhysicalName() const override {
			return m_owner != nullptr ? wxString::Format(wxT("fld%i"), m_owner->GetMetaID()) : wxString();
		}
		virtual ibMetaID GetColumnId() const override { return m_owner != nullptr ? m_owner->GetMetaID() : 0; }
		virtual ibTypeDescription& GetTypeDesc() const override { return m_owner->GetTypeDesc(); }
		virtual bool FillCheck() const override { return m_owner != nullptr && m_owner->FillCheck(); }

	private:
		ibValueMetaObjectConstant* m_owner;
	};

	// A constant VENDS its queryable — the single-row sys_const table. ibConstantQueryable (a friend)
	// owns the table navigation, built from the constant's primitives (GetName / GetMetaID /
	// GetPhysicalTableName), and resolves the one column to the value column above. So
	// From(constant->GetQueryable()) reads the one row.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
	friend class ibConstantQueryable;

	const ibValueMetaObjectConstantColumn* GetValueColumn() const { return m_column; }

	// THE ONE COLUMN, offered to whoever asks what this source holds — the query constructor's
	// catalogue, a form's field picker, anything reading a source's shape. A constant reads as
	// `Constant.<Name>.Value`, and from there onwards by the value's own type, so the single node
	// carries the column's type description and a reference walks further by dot.
	// Without this the descriptor forwards to a base that fills nothing, and a constant stands in
	// the tree as a leaf with no field to select — visible, addable, and useless.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const {
		if (m_column != nullptr)
			explorer.AppendColumn(m_column->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
	}

	// --- the composite face — answered directly now, with no cast anywhere --------------------
	//
	// Show / Modify map onto the constant's own Read / Write roles, which is the whole question the
	// form was asking and could not get an answer to. The attribute list holds exactly one entry:
	// the value column. A constant IS an object with one attribute — that is the honest shape.
	virtual bool AccessRight_Show() const override { return AccessRight_Read(); }
	virtual bool AccessRight_Modify() const override { return AccessRight_Write(); }

	// Two bases declare GetMetaData — the metaobject (which HAS one) and the type factory (which
	// only needs to READ one, and declares it pure). One definition per signature answers both: a
	// using-declaration would not, since it makes a name visible without implementing anything, and
	// the class stays abstract.
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibMetaData* GetMetaData() override { return m_metaData; }

	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		array.push_back(m_column);
		return array;
	}

	// Hosts no children of its own. A constant carries a module (a predefined child, not a tree
	// node) and nothing else — no forms, no templates, no commands. Left at GenericData's default
	// it would start admitting them, which changes the designer tree rather than fixing a cast.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	// The form is built on the fly from the source (see GetObjectForm) — a constant has no form
	// metaobject, so there is no form KIND to list either.
	virtual ibFormTypeList GetFormType() const override { return ibFormTypeList(); }

	// --- the type facade — it stayed HERE, which is what the user edits ------------------------
	//
	// The type is the constant's own property, exactly as it was when the constant was an attribute:
	// same property class, same editor, same place in the inspector. The value column reads it back
	// through GetTypeDesc, so the type the user picks and the type the DDL renders are one value,
	// not two that have to be kept in step.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyType->GetValueAsTypeDesc(); }
	virtual bool FillCheck() const { return m_propertyFillCheck->GetValueAsBoolean() && GetTypeDesc().GetClsidCount() > 0; }


protected:

public:

#pragma region access
	bool AccessRight_Read() const { return IsFullAccess() || AccessRight(m_roleRead); }
	bool AccessRight_Write() const { return IsFullAccess() || AccessRight(m_roleWrite); }
#pragma endregion

	ibValueMetaObjectConstant();
	virtual ~ibValueMetaObjectConstant();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	// (No rename hook here: the type registry re-files its NAME index for every metatype in
	// ibValueMetaObject::RefreshRegisteredTypeNames — the identity is the clsid and never moves.)

	//get table name
	static wxString GetPhysicalTableName() { return wxT("sys_const"); }

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyModule->GetMetaObject(); }

	//create empty object
	virtual ibValueRecordDataObjectConstant* CreateRecordDataObjectValue() const;

	// GenericData's contract. A constant DOES have a manager (ibValueManagerDataObjectConstant), but
	// it is built by the constant's own type ctor (constantCtor.h) and descends from
	// ibValueManagerObject rather than from ibValueManagerDataObject — a single global value has no
	// record-collection surface to offer. Answering nullptr says exactly that; wiring the existing
	// manager in here would mean promoting it to a base whose methods it cannot honour.
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const override { return nullptr; }

	//support form
	virtual ibBackendValueForm* GetObjectForm() const;

	//create constant table  
	static bool CreateConstantSQLTable();
	static bool DeleteConstantSQLTable();

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Create; }

	// (NO source-command surface — a constant is a single global value, never a list. Its descriptor is the
	// query-only ibMetaQueryDescriptor, which does NOT forward select / key / columns / commands, so the constant
	// is not required to carry them. It registers only so From(constant) resolves to its one row.)

	// (no dump & restore override — sys_const is dumped generically off the snapshot, L3-3 EXTERNAL mode)

protected:

	// Declare the constant table (RECORD_KEY scaffold + the value column = the constant itself).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create) {
			return GetObjectForm();
		}

		return GetObjectForm();
	}

	//per-type node data
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

private:

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordModule"), _("Record module"));

	// The VALUE properties — moved here from the attribute base the constant used to inherit. They
	// are what the user edits, so they belong on the object the designer shows, and the inner column
	// reads them back rather than holding a second copy.
	ibPropertyCategory* m_categoryValue = ibPropertyObject::CreatePropertyCategory(wxT("Value"), _("Value"));
	ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryValue, wxT("Type"), _("Type"), ibValueTypes::TYPE_STRING);
	ibPropertyBoolean* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryValue, wxT("FillCheck"), _("Fill check"));

	// The one column of sys_const — a predefined child, created with the constant and reachable only
	// through it. No metaID of its own (it reports the constant's), so nothing to serialize.
	ibValuePtr<ibValueMetaObjectConstantColumn> m_column;

	// the L4 source descriptor — CONTAINS the vended queryable (stable for this constant's
	// life) and is registered with the factory on run / close; GetQueryable() forwards to it.
	ibMetaQueryDescriptor<ibConstantQueryable, ibValueMetaObjectConstant> m_queryable{ this };

#pragma region role 
	ibRole* m_roleRead = ibValueMetaObject::CreateRole(wxT("Read"), _("Read"));
	ibRole* m_roleWrite = ibValueMetaObject::CreateRole(wxT("Write"), _("Write"));
#pragma endregion

	friend class ibValueRecordDataObjectConstant;
	friend class ibMetaData;
};

#include "backend/moduleInfo.h"

class BACKEND_API ibValueRecordDataObjectConstant : public ibValueDynamicMembers, public ibStandardCommandSource,
	public ibSourceDataObject, public ibRuntimeModuleDataObject {
	public:
	virtual bool InitializeObject(const ibValueRecordDataObjectConstant* source = nullptr);
protected:
	enum helperAlias {
		eSystem,
		eProcUnit = g_aliasExport   // module exports go through the descriptor autobind
	};
	enum helperProp {
		eValue
	};
public:

	//override copy constructor
	ibValueRecordDataObjectConstant(const ibValueMetaObjectConstant* metaObject);
	ibValueRecordDataObjectConstant(const ibValueRecordDataObjectConstant& source);

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers; the surface is
	// supplied by FillMembers, bound in the ctor.
public:

	virtual ~ibValueRecordDataObjectConstant();

	// Constant's module object drives lazy compile creation via
	// BindContextVariable.
	virtual const class ibValueMetaObjectModuleBase* GetMetaForCompile() const override {
		return m_metaObject ? m_metaObject->GetObjectModule() : nullptr;
	}

	ibValue GetConstValue() const;
	bool SetConstValue(const ibValue& cValue);

	// Name surface = only the constant module's exported names, surfaced by the
	// descriptor autobind (ExportThunk bound in the ctor). No own filler needed.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	//override default type object 
	virtual bool IsNewObject() const { return false; }

	//is modified 
	virtual bool IsModified() const { return m_objModified; }

	//set modify 
	virtual void Modify(bool mod);

	//check is empty
	inline virtual bool IsEmpty() const { return false; }

	//check is changes data in db
	virtual bool ModifiesData() { return true; }

	//get metaData from object
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }
	// Metadata via THIS source's metaobject (it has one here).
	virtual const ibMetaData* GetSourceMetaData() const override { const auto* mo = GetMetaObject(); return mo != nullptr ? mo->GetMetaData() : nullptr; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); };

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ? stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;
	
	// ibSourceDataObject hop gate — delegates to the id primitive above.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override { return GetValueByMetaID(hop.m_id, out); }

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	//get metaData from object 
	// The constant IS a GenericData now, so this is an ordinary upcast the compiler checks. It used
	// to be a C-style cast between unrelated classes — the source of the read-only-form bug.
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const {
		return m_metaObject;
	};

	//get unique identifier
	virtual ibUniqueKey GetGuid() const { return m_metaObject->GetGuid(); }
	virtual bool SaveModify() override { return SetConstValue(m_constValue); }

	// Constants are single-row "global" - lock keyed by namespace path
	// only, no per-key sub-identifier. Soft-lock UX same as ref-objects:
	// form opens silent on conflict, Write re-throws if persistent.
	bool TryAcquireFormLock(ibLockMode mode = ibLockMode::Exclusive) override;

	//get frame
	virtual ibBackendValueForm* GetForm() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue();
	virtual ibBackendValueForm* GetFormValue();
#pragma endregion

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//Get ref class 
	virtual ibClassID GetClassType() const;
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	bool m_objModified;

	const ibValueMetaObjectConstant* m_metaObject;
	ibValue m_constValue;

	friend class ibValue;
};

#endif