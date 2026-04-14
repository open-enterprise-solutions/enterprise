#ifndef __DOCUMENT_H__
#define __DOCUMENT_H__

#include "commonObject.h"

#include "documentEnum.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectDocument : public ibValueMetaObjectRecordDataMutableRef {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectDocument);
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormObject"), _("Form object"), eFormObject);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		formList.AppendItem(wxT("FormSelect"), _("Form select"), eFormSelect);
		return formList;
	}

public:

	ibMetaDescription& GetRecordDescription() const { return m_propertyRegisterRecord->GetValueAsMetaDesc(); }

	ibValueMetaObjectAttributePredefined* GetDocumentNumber() const { return m_propertyAttributeNumber->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDocumentDate() const { return m_propertyAttributeDate->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDocumentPosted() const { return m_propertyAttributePosted->GetMetaObject(); }

	//default constructor 
	ibValueMetaObjectDocument();
	virtual ~ibValueMetaObjectDocument();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm);
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm);

	//get attribute code 
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const { return m_propertyAttributeNumber->GetMetaObject(); }

	//create associate value 
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
#pragma endregion
	//descriptions...
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleObject->GetMetaObject(); }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//predefined array 
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		array = {
			m_propertyAttributeNumber->GetMetaObject(),
			m_propertyAttributeDate->GetMetaObject(),
			m_propertyAttributePosted->GetMetaObject(),
			m_propertyAttributeReference->GetMetaObject(),
			m_propertyAttributeDeletionMark->GetMetaObject()
		};

		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		array = {
			m_propertyAttributeNumber->GetMetaObject(),
			m_propertyAttributeDate->GetMetaObject()
		};

		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create empty object
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid);

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormObject == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	bool FillFormSelect(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormSelect == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleObject = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectDocument::FillFormObject);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectDocument::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectDocument::FillFormSelect);

	ibPropertyRecord* m_propertyRegisterRecord = ibPropertyObject::CreateProperty<ibPropertyRecord>(m_categoryData, wxT("ListRegisterRecord"), _("List register record"));

	//create default attributes
	ibPropertyContainer<>* m_propertyAttributeNumber = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Number"), _("Number"), wxEmptyString, 11, true));
	ibPropertyContainer<>* m_propertyAttributeDate = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("Date"), _("Date"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributePosted = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Posted"), _("Posted"), wxEmptyString));

	friend class ibValueRecordDataObjectDocument;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectDocument : public ibValueRecordDataObjectRef {
public:
	class ibRecorderRegisterDocument : public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibRecorderRegisterDocument);
	public:

		void CreateRecordSet();
		bool WriteRecordSet();
		bool DeleteRecordSet();
		void ClearRecordSet();

		void RefreshRecordSet();

		ibRecorderRegisterDocument(ibValueRecordDataObjectDocument* currentDoc = nullptr);
		virtual ~ibRecorderRegisterDocument();

		//standart override 
		virtual ibValueMethodHelper* GetPMethods() const {
			//PrepareNames();
			return m_methodHelper;
		}

		virtual void PrepareNames() const;
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

		//check is empty
		virtual bool IsEmpty() const { return false; }

	private:
		ibValueRecordDataObjectDocument* m_document;
		std::map<ibMetaID, ibValuePtr<ibValueRecordSetObject>> m_records;
		ibValueMethodHelper* m_methodHelper;
	};
protected:
	ibValueRecordDataObjectDocument(ibValueMetaObjectDocument* metaObject = nullptr, const ibGuid& guid = wxNullGuid);
	ibValueRecordDataObjectDocument(const ibValueRecordDataObjectDocument& source);
public:
	virtual ~ibValueRecordDataObjectDocument();

	bool IsPosted() const;

	void ClearRecordSet() {
		wxASSERT(m_registerRecords);
		m_registerRecords->ClearRecordSet();
	}

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
			IsPosted() ? ibDocumentWriteMode::ibDocumentWriteMode_Posting : ibDocumentWriteMode::ibDocumentWriteMode_Write,
			ibDocumentPostingMode::ibDocumentPostingMode_Regular
		);
	}

	//default methods
	virtual bool FillObject(ibValue& vFillObject) const {
		return Filling(vFillObject);
	}

	virtual ibValueRecordDataObjectRef* CopyObject(bool showValue = false) {
		ibValueRecordDataObjectRef* objectRef = CopyObjectValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}

	virtual bool WriteObject() {
		return WriteObject(
			IsPosted() ? ibDocumentWriteMode::ibDocumentWriteMode_Posting : ibDocumentWriteMode::ibDocumentWriteMode_Write,
			ibDocumentPostingMode::ibDocumentPostingMode_Regular
		);
	}
	virtual bool WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode);
	virtual bool DeleteObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
#pragma endregion

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

public:
	virtual void SetDeletionMark(bool deletionMark = true);
private:
	ibValuePtr<ibRecorderRegisterDocument> m_registerRecords;
	friend class ibValue;
};

#endif