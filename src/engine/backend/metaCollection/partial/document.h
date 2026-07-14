#ifndef __DOCUMENT_H__
#define __DOCUMENT_H__

#include "commonObject.h"

#include "documentEnum.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectDocument : public ibValueMetaObjectRecordDataMutableRef {
	public:
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

	enum {                          // this document's OWN posting commands (add to the inherited writeable set)
		ePostValue = 5,             // WriteObject Posting
		eClearPostingValue = 6,     // WriteObject UndoPosting
	};

	// source COMMANDS — the writeable-ref base set PLUS Post / ClearPosting. CallAsCommand loads the document by
	// key and writes it in the posting / undo-posting mode; the rest delegates to the base. Bodies in documentAction.cpp.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

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
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion
	//descriptions...
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	// Additive contract — chains to MutableRef. Document adds Number,
	// Date, Posted (its signature trio) on top of Ref/DeletionMark/
	// DataVersion.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeNumber->GetMetaObject());
		array.push_back(m_propertyAttributeDate->GetMetaObject());
		array.push_back(m_propertyAttributePosted->GetMetaObject());
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
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectDocument::FillFormObject);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectDocument::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectDocument::FillFormSelect);

	ibPropertyRecord* m_propertyRegisterRecord = ibPropertyObject::CreateProperty<ibPropertyRecord>(m_categoryData, wxT("ListRegisterRecord"), _("List register record"));

	//create default attributes
	ibPropertyContainer<>* m_propertyAttributeNumber = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Number"), _("Number"), wxEmptyString, 11, true, ibItemMode::ibItemMode_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributeDate = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("Date"), _("Date"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true, ibItemMode::ibItemMode_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributePosted = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Posted"), _("Posted"), wxEmptyString));

	friend class ibValueRecordDataObjectDocument;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectDocument : public ibValueRecordDataObjectRecorderRef {
	public:
	ibValueRecordDataObjectDocument(const ibValueMetaObjectDocument* metaObject = nullptr, const ibGuid& guid = wxNullGuid);
	ibValueRecordDataObjectDocument(const ibValueRecordDataObjectDocument& source);
	virtual ~ibValueRecordDataObjectDocument();

	// ibRecorderRegister + m_registerRecords + ClearRecordSet /
	// UpdateRecordSet + WriteObject / DeleteObject / SaveModify
	// scaffold all live on ibValueRecordDataObjectRecorderRef.
	// Document only contributes leaf-specific hook overrides below.

	// Hook overrides for Document-specific posting semantics. See
	// ibValueRecordDataObjectRecorderRef in commonObject.h.
	virtual bool IsPosted() const override;
	virtual bool CheckDeletionMarkOnPosting(ibDocumentWriteMode wm) const override;
	virtual void ApplyPostedAttributeOnWrite(ibDocumentWriteMode wm) override;
	virtual void FillDefaultDateForNew() override;
	virtual const ibMetaDescription* GetRecordDescription() const override;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	// Document's own methods (bound in the ctor). Data members come from the base
	// FillDataMembers. ThisObject.RegisterRecords is surfaced by the descriptor's
	// ExportThunk tail-bind (moduleInfo.h), not by a contributor here.
	void FillMethods(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// ShowFormValue / GetFormValue inherited from base. Document has
	// a single form-id (no folder/item branching) — hook below.
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return ibValueMetaObjectDocument::eFormObject;
	}
public:

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	// SetDeletionMark inherited from ibValueRecordDataObjectRecorderRef
	// (un-post then base SetDeletionMark) — common algorithm across
	// recorder-flavour ref-objects.

private:
	friend class ibValue;
};

#endif