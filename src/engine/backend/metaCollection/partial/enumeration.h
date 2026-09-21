#ifndef __ENUMERATION_H__
#define __ENUMERATION_H__

#include "commonObject.h"

class ibValueMetaObjectEnumeration : public ibValueMetaObjectRecordDataEnumRef {
	public:


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

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaEnumCLSID)
			return clsid;
		return ibValueMetaObjectGenericData::ResolveChild(clsid);
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
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const { return nullptr; }
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return nullptr; }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

protected:

	// FillArrayObjectByPredefinedAttribute inherited from the Ref base —
	// Enumeration has no extra predefined attributes beyond Reference.

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return true;
	}

	//create manager
	virtual ibValuePtr<ibValueManagerDataObject> CreateManagerDataObjectValue() const;

	//create object data with meta form
	virtual ibSourcePtr<ibSourceDataObject> CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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

	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"), _("Code of the enumeration: its exported procedures and functions are called on the manager, as Enumerations.<Name>.<Function>(). An enumeration has no object module - its values are declared, not written."));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), _("The form the list of the enumeration's values opens with. Empty: the list form is generated."), &ibValueMetaObjectEnumeration::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), _("The form used to choose a value for a field of this enumeration's type. Empty: a drop-down of the values."), &ibValueMetaObjectEnumeration::FillFormSelect);
};

#endif