#ifndef _DATAPROCESSOR_H__
#define _DATAPROCESSOR_H__

#include "object.h"

class CMetaObjectDataProcessor : public IMetaObjectRecordDataExt {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessor);
protected:

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

public:

	enum
	{
		eFormDataProcessor = 1,
	};

	virtual OptionList* GetFormType() override {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formDataProcessor"), _("Form data processor"), eFormDataProcessor);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms"});
	Property* m_propertyDefFormObject = IPropertyObject::CreateProperty(m_categoryForm, { "default_object", "default object" }, &CMetaObjectDataProcessor::GetFormObject, wxNOT_FOUND);

private:
	OptionList* GetFormObject(PropertyOption*);
public:

	form_identifier_t GetDefFormObject() const {
		return m_propertyDefFormObject->GetValueAsInteger();
	}

	void SetDefFormObject(const form_identifier_t &id) const {
		m_propertyDefFormObject->SetValue(id);
	}

	CMetaObjectDataProcessor(int objMode = METAOBJECT_NORMAL);
	virtual ~CMetaObjectDataProcessor();

	virtual wxString GetClassName() const { 
		return wxT("dataProcessor");
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

	//suppot form
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IControlFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const {
		return m_moduleObject; 
	}
	
	virtual CMetaCommonModuleObject* GetModuleManager() const { 
		return m_moduleManager; 
	}

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:
	//create empty object
	virtual IRecordDataObjectExt* CreateObjectExtValue();  //create object 
	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CObjectDataProcessor;
};

#define default_meta_id 10 //for dataProcessors

class CMetaObjectDataProcessorExternal : public CMetaObjectDataProcessor {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessorExternal);
public:
	CMetaObjectDataProcessorExternal() : CMetaObjectDataProcessor(METAOBJECT_EXTERNAL) { 
		m_metaId = default_meta_id; 
	}
	virtual wxString GetClassName() const { 
		return wxT("externalDataProcessor"); 
	}
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CObjectDataProcessor : public IRecordDataObjectExt {
protected:
	CObjectDataProcessor(CMetaObjectDataProcessor* metaObject);
	CObjectDataProcessor(const CObjectDataProcessor& source);
public:
	
	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IControlFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IControlFrame* owner = NULL);

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

protected:
	friend class CMetaObjectDataProcessor;
	friend class CExternalDataProcessorModuleManager;
};

#endif