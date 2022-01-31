#ifndef _DATAPROCESSOR_H__
#define _DATAPROCESSOR_H__

#include "baseObject.h"

class CMetaObjectDataProcessorValue : public IMetaObjectValue {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessorValue);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject *m_moduleObject;
	CMetaCommonModuleObject *m_moduleManager;

	enum
	{
		eFormDataProcessor = 1,
	};

	virtual OptionList *GetFormType() override
	{
		OptionList *optionlist = new OptionList;
		optionlist->AddOption("formDataProcessor", eFormDataProcessor);
		return optionlist;
	}

	//default form 
	int m_defaultFormObject;

	//external or default dataProcessor
	int m_objMode;

private:

	OptionList *GetFormObject(Property *);

protected:

	CMetaObjectDataProcessorValue(int objMode);

public:

	CMetaObjectDataProcessorValue();
	virtual ~CMetaObjectDataProcessorValue();

	virtual wxString GetClassName() const { return wxT("dataProcessor"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject *metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject *metaForm);

	//create associate value 
	virtual CMetaFormObject *GetDefaultFormByID(form_identifier_t id);

	//create object data with metaForm
	virtual IDataObjectSource *CreateObjectData(IMetaFormObject *metaObject);

	//create empty object
	virtual IDataObjectValue *CreateObjectValue();

	//create form with data 
	virtual CValueForm *CreateObjectValue(IMetaFormObject *metaForm);

	//suppot form
	virtual CValueForm *GetObjectForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid());

	//get module object in compose object 
	virtual CMetaModuleObject *GetModuleObject() { return m_moduleObject; }
	virtual CMetaCommonModuleObject *GetModuleManager() { return m_moduleManager; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu *defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	friend class CObjectDataProcessorValue;
};

#define default_meta_id 10 //for dataProcessors

class CMetaObjectDataProcessorExternalValue : public CMetaObjectDataProcessorValue {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDataProcessorExternalValue);
public:
	CMetaObjectDataProcessorExternalValue() : CMetaObjectDataProcessorValue(METAOBJECT_EXTERNAL) { m_metaId = default_meta_id; }
	virtual ~CMetaObjectDataProcessorExternalValue() {}

	virtual wxString GetClassName() const { return wxT("externalDataProcessor"); }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectDataProcessorValue : public IDataObjectValue {
	virtual void PrepareEmptyObject();
public:
	virtual bool InitializeObject();

	CObjectDataProcessorValue(const CObjectDataProcessorValue &source);
	CObjectDataProcessorValue(CMetaObjectDataProcessorValue *metaObject);
	virtual ~CObjectDataProcessorValue();

	//copy new object
	virtual IDataObjectValue *CopyObjectValue() {
		return new CObjectDataProcessorValue(*this);
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t &aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);
	virtual CValue GetAttribute(attributeArg_t &aParams);

	//support show 
	virtual void ShowFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL);
	virtual CValueForm *GetFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL);

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const { return m_metaObject; };

	//get unique identifier 
	virtual Guid GetGuid() const { return m_objGuid; }

	//check is empty
	virtual inline bool IsEmpty() const override { return false; }

	//support actions
	virtual actionData_t GetActions(form_identifier_t formType);
	virtual void ExecuteAction(action_identifier_t action, CValueForm *srcForm);

protected:

	CMetaObjectDataProcessorValue *m_metaObject;

	friend class CExternalDataProcessorModuleManager;
};

#endif