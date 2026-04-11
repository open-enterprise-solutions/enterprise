#ifndef __ENUMERATION_H__
#define __ENUMERATION_H__

#include "commonObject.h"

class ibValueMetaObjectEnumeration : public ibValueMetaObjectRecordDataEnumRef {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectEnumeration);

	enum
	{
		ID_METATREE_OPEN_MANAGER = 19000,
	};

	enum
	{
		eFormList = 2,
		eFormSelect
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		formList.AppendItem(wxT("FormSelect"), _("Form select"), eFormSelect);
		return formList;
	}

public:

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (clsid == g_metaEnumCLSID)
			return true;
		return ibValueMetaObjectGenericData::FilterChild(clsid);
	}

	ibValueMetaObjectEnumeration();
	virtual ~ibValueMetaObjectEnumeration();

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
	//support form 
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) { return nullptr; }
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
#pragma endregion

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return nullptr; }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	//descriptions...
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//predefined array 
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeReference->GetMetaObject() };
		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

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

	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectEnumeration::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectEnumeration::FillFormSelect);
};

#endif