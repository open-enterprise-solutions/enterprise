#ifndef _CATALOG_H__
#define _CATALOG_H__

#include "object.h"

#include "catalogVariant.h"
#include "catalogData.h"

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class CMetaObjectCatalog : public IMetaObjectRecordDataFolderMutableRef {
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
		eFormGroup,
		eFormList,
		eFormSelect,
		eFormFolderSelect
	};

	virtual OptionList* GetFormType() override {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formObject"),	  _("Form object"),	      eFormObject);
		optionlist->AddOption(wxT("formFolder"),	  _("Form group"),        eFormGroup);
		optionlist->AddOption(wxT("formList"),		  _("Form list"),         eFormList);
		optionlist->AddOption(wxT("formSelect"),	  _("Form select"),       eFormSelect);
		optionlist->AddOption(wxT("formGroupSelect"), _("Form group select"), eFormFolderSelect);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });

	Property* m_propertyDefFormObject		= IPropertyObject::CreateProperty(m_categoryForm, { "default_object",		_("default object") },	     &CMetaObjectCatalog::GetFormObject,      wxNOT_FOUND);
	Property* m_propertyDefFormFolder		= IPropertyObject::CreateProperty(m_categoryForm, { "default_folder",		_("default folder") },		 &CMetaObjectCatalog::GetFormFolder,      wxNOT_FOUND);
	Property* m_propertyDefFormList			= IPropertyObject::CreateProperty(m_categoryForm, { "default_list",			_("default list") },		 &CMetaObjectCatalog::GetFormList,	      wxNOT_FOUND);
	Property* m_propertyDefFormSelect		= IPropertyObject::CreateProperty(m_categoryForm, { "default_select",		_("default select") },		 &CMetaObjectCatalog::GetFormSelect,      wxNOT_FOUND);
	Property* m_propertyDefFormFolderSelect	= IPropertyObject::CreateProperty(m_categoryForm, { "default_folder_select", _("default folder select") }, &CMetaObjectCatalog::GetFormFolderSelect, wxNOT_FOUND);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Property* m_propertyOwners				= IPropertyObject::CreateOwnerProperty(m_categoryData, "owners");

private:

	//default attributes 
	CMetaDefaultAttributeObject* m_attributeOwner;

	//variant data
	ownerData_t m_ownerData;

private:

	OptionList* GetFormObject(PropertyOption*);
	OptionList* GetFormFolder(PropertyOption*);
	OptionList* GetFormList(PropertyOption*);
	OptionList* GetFormSelect(PropertyOption*);
	OptionList* GetFormFolderSelect(PropertyOption*);

public:

	CMetaDefaultAttributeObject* GetCatalogOwner() const {
		return m_attributeOwner;
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
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t& id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create form with data 
	virtual CValueForm* CreateObjectForm(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetFolderForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetFolderSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const {
		return m_moduleObject;
	}

	virtual CMetaCommonModuleObject* GetModuleManager() const {
		return m_moduleManager;
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property);

protected:

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);	
	void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;
	
	//create empty object
	virtual IRecordDataObjectFolderRef* CreateObjectRefValue(eObjectMode mode, const Guid& guid = wxNullGuid);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	friend class CObjectCatalog;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectCatalog : public IRecordDataObjectFolderRef {
	CObjectCatalog(CMetaObjectCatalog* metaObject, const Guid& objGuid = wxNullGuid, eObjectMode objMode = eObjectMode::OBJECT_ITEM);
	CObjectCatalog(const CObjectCatalog& source);
public:

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	//save modify 
	virtual bool SaveModify() {
		return WriteObject();
	}

	//default methods
	virtual bool FillObject(CValue& vFillObject);
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
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

protected:
	friend class CMetaObjectCatalog;
};

#endif