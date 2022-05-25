#ifndef _REPORT_H__
#define _REPORT_H__

#include "baseObject.h"

class CMetaObjectReport : public IMetaObjectRecordData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectReport);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormReport = 1,
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption("formReport", eFormReport);
		return optionlist;
	}

	//default form 
	int m_defaultFormObject;
	//external or default dataProcessor
	int m_objMode;

private:

	OptionList* GetFormObject(Property*);


protected:

	CMetaObjectReport(int objMode);

public:

	CMetaObjectReport();
	virtual ~CMetaObjectReport();

	virtual wxString GetClassName() const { return wxT("report"); }

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

	friend class CObjectReport;
};

#define default_meta_id 10 //for reports

class CMetaObjectReportExternal : public CMetaObjectReport {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectReportExternal);
public:
	CMetaObjectReportExternal() : CMetaObjectReport(METAOBJECT_EXTERNAL) { m_metaId = default_meta_id; }
	virtual ~CMetaObjectReportExternal() {}

	virtual wxString GetClassName() const { return wxT("externalReport"); }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectReport : public IRecordDataObject {
public:
	virtual bool InitializeObject();

	CObjectReport(const CObjectReport& source);
	CObjectReport(CMetaObjectReport* metaObject);
	virtual ~CObjectReport();

	//copy new object
	virtual IRecordDataObject* CopyObjectValue() {
		return new CObjectReport(*this);
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

	CMetaObjectReport* m_metaObject;

	friend class CExternalReportModuleManager;
};

#endif