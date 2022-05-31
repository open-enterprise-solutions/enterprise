#ifndef _CONSTANTS_H__
#define _CONSTANTS_H__

#include "metadata/metaObjects/objects/baseObject.h"

class CConstantObject;

class CMetaConstantObject : public CMetaAttributeObject,
	public IMetaCommandData {
	wxDECLARE_DYNAMIC_CLASS(CMetaConstantObject);

	enum
	{
		ID_METATREE_OPEN_CONSTANT_MANAGER = 19000,
	};

	CMetaModuleObject* m_moduleObject;

public:

	CMetaConstantObject();
	virtual ~CMetaConstantObject();

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("constant");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get table name
	static wxString GetTableNameDB() {
		return wxT("consts");
	}

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const {
		return m_moduleObject;
	}

	//create empty object
	virtual CConstantObject* CreateObjectValue();

	//get default form 
	virtual CValueForm* GetDefaultCommandForm() {
		return GetObjectForm();
	};

	//support form 
	virtual CValueForm* GetObjectForm();

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	virtual bool DeleteData();

	//process default query
	int ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	friend class CConstantObject;

protected:

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#include "common/moduleInfo.h"

class CConstantObject : public CValue, public IActionSource,
	public ISourceDataObject, public IModuleInfo {
	virtual bool InitializeObject(const CConstantObject* source = NULL);
public:

	CValue GetConstValue() const;
	bool SetConstValue(const CValue& cValue);

	CConstantObject(CMetaConstantObject* metaObject);
	CConstantObject(const CConstantObject& source);

	//standart override 
	virtual CMethods* GetPMethods() const {
		return CValue::GetPMethods();
	}

	virtual void PrepareNames() const {
		CValue::PrepareNames();
	}

	virtual CValue Method(methodArg_t& aParams) {
		return CValue::Method(aParams);
	}

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal) {
		CValue::SetAttribute(aParams, cVal);
	}

	virtual CValue GetAttribute(attributeArg_t& aParams) {
		return CValue::GetAttribute(aParams);
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return CValue::IsEmpty();
	}

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetTable(IValueTable*& tableValue, const meta_identifier_t& id);

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const {
		return (IMetaObjectWrapperData*)m_metaObject;
	};

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_metaObject->GetGuid();
	}

	virtual bool SaveModify() override {
		return SetConstValue(m_constVal);
	}

	//get frame
	virtual CValueForm* GetForm() const;

	//support show 
	virtual void ShowFormValue();
	virtual CValueForm* GetFormValue();

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

	//default showing
	virtual void ShowValue() override {
		ShowFormValue();
	}

	//Get ref class 
	virtual CLASS_ID GetClassType() const;
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const {
		return this;
	}

protected:

	CMetaConstantObject* m_metaObject;
	CValue m_constVal;
};

#endif