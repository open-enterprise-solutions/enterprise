#ifndef _DATAPROCESSOR_H__
#define _DATAPROCESSOR_H__

#include "baseObject.h"

class CMetaObjectDataProcessor : public IMetaObjectRecordData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessor);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormDataProcessor = 1,
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption("formDataProcessor", eFormDataProcessor);
		return optionlist;
	}

	//default form 
	int m_defaultFormObject;

	//external or default dataProcessor
	int m_objMode;

private:

	OptionList* GetFormObject(Property*);

protected:

	CMetaObjectDataProcessor(int objMode);

public:

	CMetaObjectDataProcessor();
	virtual ~CMetaObjectDataProcessor();

	virtual wxString GetClassName() const { return wxT("dataProcessor"); }

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
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t &id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create empty object
	virtual IRecordDataObject* CreateObjectValue();

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//suppot form
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return m_moduleObject; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CObjectDataProcessor;
};

#define default_meta_id 10 //for dataProcessors

class CMetaObjectDataProcessorExternal : public CMetaObjectDataProcessor {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessorExternal);
public:
	CMetaObjectDataProcessorExternal() : CMetaObjectDataProcessor(METAOBJECT_EXTERNAL) { m_metaId = default_meta_id; }
	virtual ~CMetaObjectDataProcessorExternal() {}

	virtual wxString GetClassName() const { return wxT("externalDataProcessor"); }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectDataProcessor : public IRecordDataObject {
public:
	virtual bool InitializeObject();

	CObjectDataProcessor(const CObjectDataProcessor& source);
	CObjectDataProcessor(CMetaObjectDataProcessor* metaObject);
	virtual ~CObjectDataProcessor();

	//copy new object
	virtual IRecordDataObject* CopyObjectValue() {
		return new CObjectDataProcessor(*this);
	}

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

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const { return m_metaObject; }

	//get unique identifier 
	virtual CUniqueKey GetGuid() const { return m_objGuid; }

	//check is empty
	virtual inline bool IsEmpty() const override { return false; }

	//support actions
	virtual actionData_t GetActions(const form_identifier_t &formType);
	virtual void ExecuteAction(const action_identifier_t &action, CValueForm* srcForm);

protected:

	CMetaObjectDataProcessor* m_metaObject;

	friend class CExternalDataProcessorModuleManager;
};

#endif