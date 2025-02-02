#ifndef _CATALOG_H__
#define _CATALOG_H__

#include "object.h"
#include "reference/reference.h"

#include "catalogVariant.h"
#include "catalogData.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class CMetaObjectCatalog : public IMetaObjectRecordDataFolderMutableRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectCatalog);
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaObjectModule* m_moduleObject;
	CMetaObjectCommonModule* m_moduleManager;

	enum
	{
		eFormObject = 1,
		eFormGroup,
		eFormList,
		eFormSelect,
		eFormFolderSelect
	};

	virtual OptionList* GetFormType() override {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formObject"), _("Form object"), eFormObject);
		optionlist->AddOption(wxT("formFolder"), _("Form group"), eFormGroup);
		optionlist->AddOption(wxT("formList"), _("Form list"), eFormList);
		optionlist->AddOption(wxT("formSelect"), _("Form select"), eFormSelect);
		optionlist->AddOption(wxT("formGroupSelect"), _("Form group select"), eFormFolderSelect);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });

	Property* m_propertyDefFormObject = IPropertyObject::CreateProperty(m_categoryForm, { "default_object",		_("default object") }, &CMetaObjectCatalog::GetFormObject, wxNOT_FOUND);
	Property* m_propertyDefFormFolder = IPropertyObject::CreateProperty(m_categoryForm, { "default_folder",		_("default folder") }, &CMetaObjectCatalog::GetFormFolder, wxNOT_FOUND);
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list",			_("default list") }, &CMetaObjectCatalog::GetFormList, wxNOT_FOUND);
	Property* m_propertyDefFormSelect = IPropertyObject::CreateProperty(m_categoryForm, { "default_select",		_("default select") }, &CMetaObjectCatalog::GetFormSelect, wxNOT_FOUND);
	Property* m_propertyDefFormFolderSelect = IPropertyObject::CreateProperty(m_categoryForm, { "default_folder_select", _("default folder select") }, &CMetaObjectCatalog::GetFormFolderSelect, wxNOT_FOUND);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Property* m_propertyOwners = IPropertyObject::CreateOwnerProperty(m_categoryData, "owners");

private:

	//default attributes 
	CMetaObjectAttributeDefault* m_attributeOwner;

	//variant data
	ownerData_t m_ownerData;

private:

	OptionList* GetFormObject(PropertyOption*);
	OptionList* GetFormFolder(PropertyOption*);
	OptionList* GetFormList(PropertyOption*);
	OptionList* GetFormSelect(PropertyOption*);
	OptionList* GetFormFolderSelect(PropertyOption*);

public:

	CMetaObjectAttributeDefault* GetCatalogOwner() const {
		return m_attributeOwner;
	}

	//default constructor 
	CMetaObjectCatalog();
	virtual ~CMetaObjectCatalog();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
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
	virtual void OnCreateFormObject(IMetaObjectForm* metaForm);
	virtual void OnRemoveMetaForm(IMetaObjectForm* metaForm);

	//get attribute code 
	virtual IMetaObjectAttribute* GetAttributeForCode() const {
		return m_attributeCode;
	}

	//override base objects 
	virtual std::vector<IMetaObjectAttribute*> GetDefaultAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaObjectAttribute*> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaObjectForm* GetDefaultFormByID(const form_identifier_t& id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaObjectForm* metaObject);

	//create form with data 
	virtual IBackendValueForm* CreateObjectForm(IMetaObjectForm* metaForm) override {
		return metaForm->GenerateFormAndRun(
			nullptr, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual IBackendValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetFolderForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetListForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetFolderSelectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);

	//descriptions...
	wxString GetDataPresentation(const IObjectValueInfo* objValue) const;

	//get module object in compose object 
	virtual CMetaObjectModule* GetModuleObject() const {
		return m_moduleObject;
	}

	virtual CMetaObjectCommonModule* GetModuleManager() const {
		return m_moduleManager;
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);
	void SaveToVariant(wxVariant& variant, IMetaData* metaData) const;

	//create empty object
	virtual IRecordDataObjectFolderRef* CreateObjectRefValue(eObjectMode mode, const Guid& guid = wxNullGuid);

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:
	friend class IMetaData;
	friend class CRecordDataObjectCatalog;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CRecordDataObjectCatalog : public IRecordDataObjectFolderRef {
	CRecordDataObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& objGuid = wxNullGuid, eObjectMode objMode = eObjectMode::OBJECT_ITEM);
	CRecordDataObjectCatalog(const CRecordDataObjectCatalog& source);
public:

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	//save modify 
	virtual bool SaveModify() {
		return WriteObject();
	}

	//default methods
	virtual bool FillObject(CValue& vFillObject) const {
		return Filling(vFillObject);
	}
	virtual IRecordDataObjectRef* CopyObject(bool showValue = false) {
		IRecordDataObjectRef* objectRef = CopyObjectValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}
	virtual bool WriteObject();
	virtual bool DeleteObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* owner = nullptr);
	virtual IBackendValueForm* GetFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* owner = nullptr);

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

protected:
	friend class IMetaData;
	friend class CMetaObjectCatalog;
};

#endif