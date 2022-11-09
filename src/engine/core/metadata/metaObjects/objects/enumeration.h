#ifndef _ENUMERATION_H__
#define _ENUMERATION_H__

#include "object.h"

class CMetaEnumerationObject;

class CMetaObjectEnumeration : public IMetaObjectRecordDataRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectEnumeration);

	enum
	{
		ID_METATREE_OPEN_MANAGER = 19000,
	};

	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormList = 1,
		eFormSelect
	};

	virtual OptionList* GetFormType() override {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formList"), _("Form list"), eFormList);
		optionlist->AddOption(wxT("formSelect"), _("Form select"), eFormSelect);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list", "default list" }, &CMetaObjectEnumeration::GetFormList, wxNOT_FOUND);
	Property* m_propertyDefFormSelect = IPropertyObject::CreateProperty(m_categoryForm, { "default_select" , "default select" }, &CMetaObjectEnumeration::GetFormSelect, wxNOT_FOUND);

private:

	//default attributes 
	CMetaDefaultAttributeObject* m_attributeOrder;

private:

	OptionList* GetFormList(PropertyOption*);
	OptionList* GetFormSelect(PropertyOption*);

public:

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (clsid == g_metaEnumCLSID)
			return true;
		return IMetaObjectWrapperData::FilterChild(clsid);
	}

	CMetaObjectEnumeration();
	virtual ~CMetaObjectEnumeration();

	virtual wxString GetClassName() const {
		return wxT("enumeration");
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
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm);

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
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) { return NULL; }
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const {
		return NULL;
	}

	virtual CMetaCommonModuleObject* GetModuleManager() const {
		return m_moduleManager;
	}

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif