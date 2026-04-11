#ifndef __DATA_PROCESSOR_H__
#define __DATA_PROCESSOR_H__

#include "commonObject.h"

class ibValueMetaObjectDataProcessor : public ibValueMetaObjectRecordDataExt {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectDataProcessor);
protected:

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

public:

	enum
	{
		eFormDataProcessor = 1,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormDataProcessor"), _("Form data processor"), eFormDataProcessor);
		return formList;
	}

public:

	ibFormID GetDefFormObject() const {
		return m_propertyDefFormObject->GetValueAsInteger();
	}

	void SetDefFormObject(const ibFormID& id) const {
		m_propertyDefFormObject->SetValue(id);
	}

	ibValueMetaObjectDataProcessor();
	virtual ~ibValueMetaObjectDataProcessor();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

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

	//create associate value 
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//suppot form
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
#pragma endregion

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleObject->GetMetaObject(); }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create empty object
	virtual ibValueRecordDataObjectExt* CreateObjectExtValue();  //create object 

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormDataProcessor == object->GetTypeForm()) {
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
	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectDataProcessor::FillFormObject);

	friend class ibValueRecordDataObjectDataProcessor;
	friend class ibMetaData;
};

#define default_meta_id 10 //for dataProcessors

class ibValueMetaObjectExternalDataProcessor : public ibValueMetaObjectDataProcessor {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectExternalDataProcessor);
public:
	ibValueMetaObjectExternalDataProcessor() : ibValueMetaObjectDataProcessor() {
		m_metaId = default_meta_id;
	}

