#ifndef _CATALOG_H__
#define _CATALOG_H__

#include "baseObject.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class CObjectCatalog;

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class wxVariantOwnerData : public wxVariantData {

	wxString MakeString() const;

public:

	void AppendRecord(meta_identifier_t record_id) {
		m_ownerData.insert(record_id);
	}

	bool Contains(meta_identifier_t record_id) const {
		auto itFounded = m_ownerData.find(record_id);
		return itFounded != m_ownerData.end();
	}

	meta_identifier_t GetById(unsigned int idx) const {
		if (m_ownerData.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_ownerData.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	unsigned int GetCount() const {
		return m_ownerData.size();
	}

	bool Eq(wxVariantData& data) const {
		return true;
	}

	wxVariantOwnerData(IMetadata* metaData) : wxVariantData(), m_metaData(metaData) {}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	wxString GetType() const {
		return wxT("wxVariantOwnerData");
	}

protected:
	IMetadata* m_metaData;
	std::set<meta_identifier_t> m_ownerData;
};

class CMetaObjectCatalog : public IMetaObjectRecordDataMutableRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectCatalog);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption("formObject", eFormObject);
		optionlist->AddOption("formList", eFormList);
		optionlist->AddOption("formSelect", eFormSelect);
		return optionlist;
	}

private:

	//default form 
	int m_defaultFormObject;
	int m_defaultFormList;
	int m_defaultFormSelect;

	//default attributes 
	CMetaDefaultAttributeObject* m_attributeCode;
	CMetaDefaultAttributeObject* m_attributeDescription;
	CMetaDefaultAttributeObject* m_attributeOwner;
	CMetaDefaultAttributeObject* m_attributeParent;
	CMetaDefaultAttributeObject* m_attributeIsFolder;

	//variant 
	class ownerData_t {
		std::set<meta_identifier_t> m_ownerData;
	public:

		bool LoadData(CMemoryReader& dataReader);

		bool LoadFromVariant(const wxVariant& variant);

		bool SaveData(CMemoryWriter& dataWritter);

		void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

		std::set<meta_identifier_t>& GetData() {
			return m_ownerData;
		}
	};

	ownerData_t m_ownerData;

private:

	OptionList* GetFormObject(Property*);
	OptionList* GetFormList(Property*);
	OptionList* GetFormSelect(Property*);

public:

	CMetaDefaultAttributeObject* GetCatalogCode() const {
		return m_attributeCode;
	}

	CMetaDefaultAttributeObject* GetCatalogDescription() const {
		return m_attributeDescription;
	}

	CMetaDefaultAttributeObject* GetCatalogOwner() const {
		return m_attributeOwner;
	}

	CMetaDefaultAttributeObject* GeCatalogParent() const {
		return m_attributeParent;
	}

	CMetaDefaultAttributeObject* GeCatalogIsFolder() const {
		return m_attributeIsFolder;
	}

	//default constructor 
	CMetaObjectCatalog();
	virtual ~CMetaObjectCatalog();

	virtual wxString GetClassName() const { 
		return wxT("catalog"); 
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm);

	//get attribute code 
	virtual IMetaAttributeObject* GetAttributeForCode() const {
		return m_attributeCode;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaAttributeObject*> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t &id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create empty group
	virtual IRecordDataObjectRef* CreateGroupObjectRefValue();
	virtual IRecordDataObjectRef* CreateGroupObjectRefValue(const Guid& guid);

	//create empty object
	virtual IRecordDataObjectRef* CreateObjectRefValue();
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& guid);

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return m_moduleObject; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

	//read and write property 
	virtual void ReadProperty() override;
	virtual	void SaveProperty() override;

protected:

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);
	void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CObjectCatalog;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectCatalog : public IRecordDataObjectRef {
	CObjectCatalog* m_catOwner;
	CObjectCatalog* m_catParent;
public:

	CObjectCatalog(const CObjectCatalog& source);
	CObjectCatalog(CMetaObjectCatalog* metaObject, int objMode = OBJECT_NORMAL);
	CObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& guid, int objMode = OBJECT_NORMAL);

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	virtual IRecordDataObject* CopyObjectValue() {
		return new CObjectCatalog(*this);
	}

	//save modify 
	virtual bool SaveModify() { 
		return WriteObject(); 
	}

	//default methods
	virtual void FillObject(CValue& vFillObject);
	virtual CValue CopyObject();
	virtual bool WriteObject();
	virtual bool DeleteObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t& aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
	virtual CValue GetAttribute(attributeArg_t& aParams);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);

	//support actions
	virtual actionData_t GetActions(const form_identifier_t &formType);
	virtual void ExecuteAction(const action_identifier_t &action, CValueForm* srcForm);

protected:

	//group or object catalog
	int m_objMode;
};

//********************************************************************************************
//*                                      Group	                                             *
//********************************************************************************************

class CObjectCatalogGroup : public CObjectCatalog {
public:
	CObjectCatalogGroup(const CObjectCatalogGroup& source) : CObjectCatalog(source) {}
	CObjectCatalogGroup(CMetaObjectCatalog* metaObject) : CObjectCatalog(metaObject, OBJECT_GROUP) {}
	CObjectCatalogGroup(CMetaObjectCatalog* metaObject, const Guid& guid) : CObjectCatalog(metaObject, guid, OBJECT_GROUP) { }
};

#endif