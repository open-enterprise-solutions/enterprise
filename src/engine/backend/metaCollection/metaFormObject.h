#ifndef _METAFORMOBJECT_H__
#define _METAFORMOBJECT_H__

#include "metaModuleObject.h"
#include "backend/uniqueKey.h"

#define defaultFormType wxNOT_FOUND
#define formDefaultName wxT("Form")

// -----------------------------------------------------------------------
// ibBackendCommandItem
// -----------------------------------------------------------------------

class BACKEND_API ibBackendCommandItem {
public:

	virtual ~ibBackendCommandItem() {}
	virtual bool ShowFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default);

protected:

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) = 0;
};

// -----------------------------------------------------------------------
// ibValueMetaObjectFormBase
// -----------------------------------------------------------------------

class BACKEND_API ibSourceDataObject;

class BACKEND_API ibValueMetaObjectFormBase : public ibValueMetaObjectModuleBase {
	public:
private:

	enum
	{
		ID_METATREE_OPEN_FORM = 19000,
	};

public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return true; }
#pragma endregion

	ibValueMetaObjectFormBase(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	bool LoadFormData(ibBackendValueForm* value) const;
	bool SaveFormData(ibBackendValueForm* value);

#pragma region _form_creator_h_

	static ibBackendValueForm* CreateAndBuildForm(const ibValueMetaObjectFormBase* creator,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullGuid);

	static ibBackendValueForm* CreateAndBuildForm(const ibValueMetaObjectFormBase* creator, const ibFormID& form_id = defaultFormType,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullGuid);

#pragma endregion 

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) = 0;
	virtual wxString GetModuleText() const = 0;

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) = 0;
	virtual wxMemoryBuffer GetFormData() const = 0;

	// copy form data — the LIVE form's control tree AS a transparent node (Child), not a
	// blob: pull the live form, save it to a node directly (no base64 round-trip).
	ibDataValue CopyFormData() const;
	bool PasteFormData();

	// node <-> runtime-blob shim ("прокладка"): the form blob already IS the binary-provider
	// node format, so the adapter is ONE provider round-trip. This lets the runtime stay
	// blob-based (SaveForm / LoadForm, the property cell, the prop-grid variant) while the
	// metadata serializes a transparent node tree — no base64 lump on disk / in JSON.
	static ibDataValue   FormBlobToNode(const wxMemoryBuffer& blob);
	static wxMemoryBuffer FormNodeToBlob(const ibDataValue& formNode);

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const = 0;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

// -----------------------------------------------------------------------
// ibValueMetaObjectForm
// -----------------------------------------------------------------------

class BACKEND_API ibValueMetaObjectForm : public ibValueMetaObjectFormBase {
	public:

public:

	ibValueMetaObjectForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertySelected(ibProperty* property);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyForm; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyForm->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyForm->GetValueAsString(); }

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) { m_propertyForm->SetValue(formData); }
	virtual wxMemoryBuffer GetFormData() const { return m_propertyForm->GetValueAsMemoryBuffer(); }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const {
		return m_properyFormType->GetValueAsInteger();
	}

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	bool FillGenericFormType(ibPropertyList* prop);
	bool FillFormType(ibPropertyList* prop) {
		prop->AppendItem(formDefaultName, defaultFormType, GetIcon());
		return FillGenericFormType(prop);
	}

	ibPropertyForm* m_propertyForm = ibPropertyObject::CreateProperty<ibPropertyForm>(m_categoryContext, wxT("FormData"), _("Form"));
	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("Form"), _("Form"));
	ibPropertyList* m_properyFormType = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("FormType"), _("Type"), &ibValueMetaObjectForm::FillFormType);
};

// -----------------------------------------------------------------------
// ibValueMetaObjectCommonForm
// -----------------------------------------------------------------------

class BACKEND_API ibValueMetaObjectCommonForm :
	public ibValueMetaObjectFormBase, public ibBackendCommandItem {
	public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Use(); }
#pragma endregion

#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	ibValueMetaObjectCommonForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyForm; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyForm->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyForm->GetValueAsString(); }

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) { m_propertyForm->SetValue(formData); }
	virtual wxMemoryBuffer GetFormData() const { return m_propertyForm->GetValueAsMemoryBuffer(); }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const { return defaultFormType; }

#pragma region _form_builder_h_
	//support form 
	ibBackendValueForm* GetObjectForm(ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion 

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//get default form
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Default)
			return GetObjectForm();

		return GetObjectForm();
	}

private:

	ibPropertyForm* m_propertyForm = ibPropertyObject::CreateProperty<ibPropertyForm>(m_categoryContext, wxT("FormData"), _("Form"));

#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

#endif 