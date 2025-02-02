#ifndef _CONSTANTS_H__
#define _CONSTANTS_H__

#include "backend/metaCollection/partial/object.h"

class CRecordDataObjectConstant;

class CMetaObjectConstant : public CMetaObjectAttribute,
	public IMetaCommandData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectConstant);
private:
	Role* m_roleRead = IMetaObject::CreateRole({ "read", _("read") });
	Role* m_roleInsert = IMetaObject::CreateRole({ "insert", _("insert") });
	Role* m_roleUpdate = IMetaObject::CreateRole({ "update", _("update") });
	Role* m_roleDelete = IMetaObject::CreateRole({ "delete", _("delete") });
protected:
	enum
	{
		ID_METATREE_OPEN_CONSTANT_MANAGER = 19000,
	};

	CMetaObjectModule* m_moduleObject;

public:

	CMetaObjectConstant();
	virtual ~CMetaObjectConstant();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get table name
	static wxString GetTableNameDB() {
		return wxT("_const");
	}

	//get module object in compose object 
	virtual CMetaObjectModule* GetModuleObject() const {
		return m_moduleObject;
	}

	//create empty object
	virtual CRecordDataObjectConstant* CreateObjectValue();

	//get default form 
	virtual IBackendValueForm* GetDefaultCommandForm() {
		return GetObjectForm();
	};

	//support form 
	virtual IBackendValueForm* GetObjectForm();

	//create constant table  
	static bool CreateConstantSQLTable();
	static bool DeleteConstantSQLTable();

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);

	//process default query
	int ProcessAttribute(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr);

private:
	friend class IMetaData;
	friend class CRecordDataObjectConstant;
protected:

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	virtual bool DeleteData();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#include "backend/wrapper/moduleInfo.h"

class CRecordDataObjectConstant : public CValue, public IActionSource,
	public ISourceDataObject, public IModuleInfo {
	virtual bool InitializeObject(const CRecordDataObjectConstant* source = nullptr);
protected:
	CRecordDataObjectConstant(CMetaObjectConstant* metaObject);
	CRecordDataObjectConstant(const CRecordDataObjectConstant& source);
public:
	
	CValue GetConstValue() const;
	bool SetConstValue(const CValue& cValue);

	inline virtual bool IsEmpty() const {
		return false;
	}

	//get metaData from object 
	virtual IMetaObjectGenericData* GetSourceMetaObject() const final {
		return GetMetaObject();
	}

	//Get ref class 
	virtual class_identifier_t GetSourceClassType() const final {
		return GetClassType();
	};

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//support source set/get data 
	virtual bool SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal);
	virtual bool GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const;

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	//get metaData from object 
	virtual IMetaObjectGenericData* GetMetaObject() const {
		return (IMetaObjectGenericData*)m_metaObject;
	};

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_metaObject->GetGuid();
	}

	virtual bool SaveModify() override {
		return SetConstValue(m_constVal);
	}

	//get frame
	virtual IBackendValueForm* GetForm() const;

	//support show 
	virtual void ShowFormValue();
	virtual IBackendValueForm* GetFormValue();

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//default showing
	virtual void ShowValue() override {
		ShowFormValue();
	}

	//Get ref class 
	virtual class_identifier_t GetClassType() const;
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const {
		return this;
	}

protected:
	CMetaObjectConstant* m_metaObject;
	CValue m_constVal;
protected:
	friend class IMetaData;
	friend class CMetaObjectConstant;
};

#endif