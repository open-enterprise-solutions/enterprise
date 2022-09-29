#ifndef _DOCUMENT_H__
#define _DOCUMENT_H__

#include "object.h"

#include "documentVariant.h"
#include "documentEnum.h"
#include "documentData.h"

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class CMetaObjectDocument : public IMetaObjectRecordDataMutableRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDocument);

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
	CMetaDefaultAttributeObject* m_attributeNumber;
	CMetaDefaultAttributeObject* m_attributeDate;
	CMetaDefaultAttributeObject* m_attributePosted;

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

	CMetaDefaultAttributeObject* GetDocumentNumber() const {
		return m_attributeNumber;
	}

	CMetaDefaultAttributeObject* GetDocumentDate() const {
		return m_attributeDate;
	}

	CMetaDefaultAttributeObject* GetDocumentPosted() const {
		return m_attributePosted;
	}

	//default constructor 
	CMetaObjectDocument();
	virtual ~CMetaObjectDocument();

	virtual wxString GetClassName() const {
		return wxT("document");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property);

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
		return m_attributeNumber;
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
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

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
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& objGuid = wxNullGuid);

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);
	void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CObjectDocument;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

#define registerRecords wxT("registerRecords")

class CObjectDocument : public IRecordDataObjectRef {
public:
	class CRecordRegister : public CValue {
		CObjectDocument* m_document;
		std::map<meta_identifier_t, IRecordSetObject*> m_records;
	public:

		void CreateRecordSet();
		bool WriteRecordSet();
		bool DeleteRecordSet();
		void ClearRecordSet();

		CRecordRegister(CObjectDocument* currentDoc = NULL);
		virtual ~CRecordRegister();

		virtual wxString GetTypeString() const {
			return wxT("recordRegister");
		}

		virtual wxString GetString() const {
			return wxT("recordRegister");
		}

		//standart override 
		virtual CMethods* GetPMethods() const;

		virtual void PrepareNames() const;
		virtual CValue Method(methodArg_t& aParams);

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
		virtual CValue GetAttribute(attributeArg_t& aParams);

		//check is empty
		virtual inline bool IsEmpty() const override {
			return false;
		}
	protected:
		CMethods* m_methods;
	};
private:
	CRecordRegister* m_registerRecords;
protected:
	CObjectDocument(CMetaObjectDocument* metaObject = NULL, const Guid& guid = wxNullGuid);
	CObjectDocument(const CObjectDocument& source);
public:
	virtual ~CObjectDocument();

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
			IsPosted() ? eDocumentWriteMode::ePosting : eDocumentWriteMode::eWrite, 
			eDocumentPostingMode::eRegular
		);
	}

	//default methods
	virtual bool FillObject(CValue& vFillObject) const;
	virtual CValue CopyObject();
	virtual bool WriteObject(eDocumentWriteMode writeMode, eDocumentPostingMode postingMode);
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
	void SetDeletionMark(bool deletionMark = true);
protected:
	friend class CMetaObjectDocument; 
};

#endif