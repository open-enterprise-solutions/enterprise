#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include "backend/metaCollection/partial/commonObject.h"

class BACKEND_API ibValueRecordDataObjectConstant;
class ibStructureBatch;   // per-table DDL/seed batch — ProcessAttribute pours the value column into it

// ibConstantQueryable — the L3 queryable for a constant (its single-row sys_const
// table). The constant no longer IS a queryable; it VENDS this adapter (a stable
// member), which forwards to the constant's own query methods. The constant is still
// a COLUMN (via ibValueMetaObjectAttribute); only the table face moved out.
class ibValueMetaObjectConstant;
class BACKEND_API ibConstantQueryable : public ibBackendQueryable {
public:
	explicit ibConstantQueryable(const ibValueMetaObjectConstant* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // the constant IS its one column
	virtual wxString GetQueryTableName() const override;
	virtual ibGuid GetQueryTableGuid() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;                      // metadata context for column-based value reads
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;   // { RECORD_KEY } — the single-row UPSERT match
private:
	const ibValueMetaObjectConstant* m_meta;
};

class BACKEND_API ibValueMetaObjectConstant :
	public ibValueMetaObjectAttribute, public ibBackendCommandItem, public ibBackendQueryableHolder {
	public:

	// A constant is BOTH a column (via ibValueMetaObjectAttribute -> the value lives as
	// one column of the shared single-row sys_const) AND, through its vended queryable,
	// that one-row table. The constant VENDS the queryable; ibConstantQueryable (a
	// friend) owns the table navigation, from the constant's primitives (GetName /
	// GetMetaID / GetPhysicalTableName). So From(constant->GetQueryable()) reads the one row.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
	friend class ibConstantQueryable;


protected:
	enum
	{
		ID_METATREE_OPEN_CONSTANT_MANAGER = 19000,
	};

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

	//get table name
	static wxString GetPhysicalTableName() { return wxT("sys_const"); }

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyModule->GetMetaObject(); }

	//create empty object
	virtual ibValueRecordDataObjectConstant* CreateRecordDataObjectValue() const;

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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

private:

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordModule"), _("Record module"));

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

class BACKEND_API ibValueRecordDataObjectConstant : public ibValueDynamicMembers, public ibActionDataObject,
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
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const {
		return (const ibValueMetaObjectGenericData*)m_metaObject;
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
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
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