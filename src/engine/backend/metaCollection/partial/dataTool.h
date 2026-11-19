#ifndef __DATA_TOOL_H__
#define __DATA_TOOL_H__

#include "commonObject.h"

class ibValueMetaObjectDataTool : public ibValueMetaObjectRecordDataExt {
	public:

	// Its own menu ids — the same numbering every metatype uses (from 19000, unique only within
	// one metaobject). Public because the external data tool's own toolbar has a button for the
	// object module and has to say WHICH item of the menu it means.
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER,
	};

	enum
	{
		eFormDataTool = 1,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(ibT("FormDataTool"), _("Form data tool"), eFormDataTool);
		return formList;
	}

public:

	ibFormID GetDefFormObject() const {
		return m_propertyDefFormObject->GetValueAsInteger();
	}

	void SetDefFormObject(const ibFormID& id) const {
		m_propertyDefFormObject->SetValue(id);
	}

	ibValueMetaObjectDataTool();
	virtual ~ibValueMetaObjectDataTool();

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

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
	virtual ibFormPtr<ibBackendValueForm> GetObjectForm(const ibFormRequest& request = ibFormRequest(), ibBackendControlFrame* ownerControl = nullptr) const;
#pragma endregion

	//get module object in compose object
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectSharedModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

protected:

	//create manager
	virtual ibValuePtr<ibValueManagerDataObject> CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValuePtr<ibValueRecordDataObjectExt> CreateObjectExtValue() const;  //create object

	//create object data with meta form
	virtual ibSourcePtr<ibSourceDataObject> CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormDataTool == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetClassType(),
					object);
			}
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, ibT("ObjectModule"), _("Object module"), _("Code of one data tool object: the procedures that do its work over its attributes and tabular sections, called from its form or from other code."));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, ibT("ManagerModule"), _("Manager module"), _("Code of the data tool kind as a whole: its exported procedures and functions are called on the manager, as DataTools.<Name>.<Function>()."));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(ibT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, ibT("DefaultFormObject"), _("Default Object Form"), _("The form the data tool opens with. Empty: a form generated from its attributes."), &ibValueMetaObjectDataTool::FillFormObject);

	friend class ibValueRecordDataObjectDataTool;
	friend class ibMetaData;
};

#define default_meta_id 10 //for dataTools

class ibValueMetaObjectExternalDataTool : public ibValueMetaObjectDataTool {
	public:
	ibValueMetaObjectExternalDataTool() : ibValueMetaObjectDataTool() {
		m_metaId = default_meta_id;
	}

	//create from file?
	virtual bool IsExternalCreate() const { return true; }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectDataTool : public ibValueRecordDataObjectExt {
	public:
	ibValueRecordDataObjectDataTool(const ibValueMetaObjectDataTool* metaObject);
	ibValueRecordDataObjectDataTool(const ibValueRecordDataObjectDataTool& source);
public:

	// ShowFormValue / GetFormValue inherited from base. DataTool
	// has a single form-id (eFormDataTool) and no CloseOnOwnerClose
	// behaviour (Ext branch keeps the default OnFormCreated no-op).
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return ibValueMetaObjectDataTool::eFormDataTool;
	}
public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:

	friend class ibValue;
	friend class ibValueMetaObjectDataTool;
	friend class ibValueModuleRuntimeManagerExternalDataTool;
};

// External data tool value object: regular data tool behaviour + a reference to the external metadata container it works in, held
// at runtime (ibExternalOwnerHelper). Embedded / application data tools use the plain ibValueRecordDataObjectDataTool.
class ibValueRecordDataObjectExternalDataTool :
	public ibValueRecordDataObjectDataTool,
	public ibExternalOwnerHelper {
public:
	ibValueRecordDataObjectExternalDataTool(const ibValueMetaObjectDataTool* metaObject, std::shared_ptr<ibMetaData> externalMetadata = nullptr)
		: ibValueRecordDataObjectDataTool(metaObject), ibExternalOwnerHelper(std::move(externalMetadata)) {}
};

#endif
