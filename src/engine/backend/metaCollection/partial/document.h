#ifndef _DOCUMENT_H__
#define _DOCUMENT_H__

#include "object.h"

#include "documentVariant.h"
#include "documentEnum.h"
#include "documentData.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class CMetaObjectDocument : public IMetaObjectRecordDataMutableRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDocument);
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
		eFormList,
		eFormSelect
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formObject"), _("Form object"), eFormObject);
		optionlist->AddOption(wxT("formList"), _("Form list"), eFormList);
		optionlist->AddOption(wxT("formSelect"), _("Form select"), eFormSelect);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms"});
	
	Property* m_propertyDefFormObject = IPropertyObject::CreateProperty(m_categoryForm, { "default_object", "default object" }, &CMetaObjectDocument::GetFormObject, wxNOT_FOUND);
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list", "default list" }, &CMetaObjectDocument::GetFormList, wxNOT_FOUND);
	Property* m_propertyDefFormSelect = IPropertyObject::CreateProperty(m_categoryForm, { "default_select", "default Sselect" }, &CMetaObjectDocument::GetFormSelect, wxNOT_FOUND);

	Property* m_propertyRecordData = IPropertyObject::CreateRecordProperty(m_categoryData, { "register_records", "register records" });

private:

	//default attributes 
	CMetaObjectAttributeDefault* m_attributeNumber;
	CMetaObjectAttributeDefault* m_attributeDate;
	CMetaObjectAttributeDefault* m_attributePosted;

	//record data 
	recordData_t m_recordData;

private:

	recordData_t& GetRecordData() {
		return m_recordData;
	}

	OptionList* GetFormObject(PropertyOption*);
	OptionList* GetFormList(PropertyOption*);
	OptionList* GetFormSelect(PropertyOption*);

public:

	CMetaObjectAttributeDefault* GetDocumentNumber() const {
		return m_attributeNumber;
	}

	CMetaObjectAttributeDefault* GetDocumentDate() const {
		return m_attributeDate;
	}

	CMetaObjectAttributeDefault* GetDocumentPosted() const {
		return m_attributePosted;
	}

	//default constructor 
	CMetaObjectDocument();
	virtual ~CMetaObjectDocument();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

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
		return m_attributeNumber;
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
	virtual IBackendValueForm* GetListForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);

	//descriptions...
	wxString GetDataPresentation(const IObjectValueInfo* objValue) const;

	//get module object in compose object 
	virtual CMetaObjectModule* GetModuleObject() const { 
		return m_moduleObject; 
	}
	
	virtual CMetaObjectCommonModule* GetModuleManager() const { 
		return m_moduleManager; 
	}

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//create empty object
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& objGuid = wxNullGuid);

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);
	void SaveToVariant(wxVariant& variant, IMetaData* metaData) const;

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:
	friend class IMetaData;
	friend class CRecordDataObjectDocument;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CRecordDataObjectDocument : public IRecordDataObjectRef {
public:
	class CRecorderRegisterDocument : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CRecorderRegisterDocument);
	private:
		CRecordDataObjectDocument* m_document;
		std::map<meta_identifier_t, IRecordSetObject*> m_records;
	public:

		void CreateRecordSet();
		bool WriteRecordSet();
		bool DeleteRecordSet();
		void ClearRecordSet();

		CRecorderRegisterDocument(CRecordDataObjectDocument* currentDoc = nullptr);
		virtual ~CRecorderRegisterDocument();

		//standart override 
		virtual CMethodHelper* GetPMethods() const {
			//PrepareNames();
			return m_methodHelper; 
		}

		virtual void PrepareNames() const;
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

		//check is empty
		virtual inline bool IsEmpty() const {
			return false;
		}
	protected:
		CMethodHelper* m_methodHelper;
	};
private:
	CRecorderRegisterDocument* m_registerRecords;
protected:
	CRecordDataObjectDocument(CMetaObjectDocument* metaObject = nullptr, const Guid& guid = wxNullGuid);
	CRecordDataObjectDocument(const CRecordDataObjectDocument& source);
public:
	virtual ~CRecordDataObjectDocument();

	bool IsPosted() const;
	void UpdateRecordSet() {
		wxASSERT(m_registerRecords);
		m_registerRecords->ClearRecordSet();
		m_registerRecords->CreateRecordSet();
	}

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	//save modify 
	virtual bool SaveModify() {
		return WriteObject(
			IsPosted() ? eDocumentWriteMode::eDocumentWriteMode_Posting : eDocumentWriteMode::eDocumentWriteMode_Write, 
			eDocumentPostingMode::eDocumentPostingMode_Regular
		);
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
	virtual bool WriteObject(eDocumentWriteMode writeMode, eDocumentPostingMode postingMode);
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
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr);
	virtual IBackendValueForm* GetFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr);

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

protected:
	void SetDeletionMark(bool deletionMark = true);
protected:
	friend class IMetaData; 
	friend class CMetaObjectDocument; 
};

#endif