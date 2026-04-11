#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include "backend/metaCollection/partial/commonObject.h"

class BACKEND_API ibValueRecordDataObjectConstant;

class BACKEND_API ibValueMetaObjectConstant : 
	public ibValueMetaObjectAttribute, public ibBackendCommandItem {
	
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectConstant);
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
	static wxString GetTableNameDB() { return wxT("sys_const"); }

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModule->GetMetaObject(); }

	//create empty object
	virtual ibValueRecordDataObjectConstant* CreateRecordDataObjectValue();

	//support form 
	virtual ibBackendValueForm* GetObjectForm();

	//create constant table  
	static bool CreateConstantSQLTable();
	static bool DeleteConstantSQLTable();

	//get command section 
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Create; }

	//process default query
	int ProcessAttribute(const wxString& tableName, ibValueMetaObjectAttributeBase* srcAttr, ibValueMetaObjectAttributeBase* dstAttr);

	// load & save config data 
	virtual bool LoadTableData(const ibReaderMemory& reader);
	virtual bool SaveTableData(ibWriterMemory& writer) const;

protected:

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create) {
			return GetObjectForm();
		}

		return GetObjectForm();
	}

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
	virtual bool DeleteData();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

private:

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordModule"), _("Record module"));

#pragma region role 
	ibRole* m_roleRead = ibValueMetaObject::CreateRole(wxT("Read"), _("Read"));
	ibRole* m_roleWrite = ibValueMetaObject::CreateRole(wxT("Write"), _("Write"));
#pragma endregion

	friend class ibValueRecordDataObjectConstant;
	friend class ibMetaData;
};

#include "backend/moduleInfo.h"

class BACKEND_API ibValueRecordDataObjectConstant : public ibValue, public ibActionDataObject,
	public ibSourceDataObject, public ibModuleDataObject {
	virtual bool InitializeObject(const ibValueRecordDataObjectConstant* source = nullptr);
protected:
	enum helperAlias {
		eSystem,
		eProcUnit
	};
	enum helperProp {
		eValue
	};
protected:

	//override copy constructor
	ibValueRecordDataObjectConstant(ibValueMetaObjectConstant* metaObject);
	ibValueRecordDataObjectConstant(const ibValueRecordDataObjectConstant& source);

	//standart override 
	virtual ibValueMethodHelper* GetPMethods() const final { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

public:

	virtual ~ibValueRecordDataObjectConstant();

	ibValue GetConstValue() const;
	bool SetConstValue(const ibValue& cValue);

	//standart override 
	virtual void PrepareNames() const;

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
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); };

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ? stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetMetaObject() const {
		return (ibValueMetaObjectGenericData*)m_metaObject;
	};

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_metaObject->GetGuid(); }
	virtual bool SaveModify() override { return SetConstValue(m_constValue); }

	//get frame
	virtual ibBackendValueForm* GetForm() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue();
	virtual ibBackendValueForm* GetFormValue();
#pragma endregion

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//Get ref class 
	virtual ibClassID GetClassType() const;
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	bool m_objModified;

	ibValueMethodHelper* m_methodHelper;
	ibValueMetaObjectConstant* m_metaObject;
	ibValue m_constValue;

	friend class ibValue;
};

#endif