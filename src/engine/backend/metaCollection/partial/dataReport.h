#ifndef __REPORT_H__
#define __REPORT_H__

#include "commonObject.h"

class ibValueMetaObjectReport : public ibValueMetaObjectRecordDataExt {
	public:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	enum
	{
		eFormReport = 1,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormReport"), _("Form report"), eFormReport);
		return formList;
	}

public:

	ibFormID GetDefFormObject() const {
		return m_propertyDefFormObject->GetValueAsInteger();
	}

	void SetDefFormObject(const ibFormID& id) const {
		m_propertyDefFormObject->SetValue(id);
	}

	ibValueMetaObjectReport();
	virtual ~ibValueMetaObjectReport();

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
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

	//get module object in compose object
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Report; }

protected:

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValueRecordDataObjectExt* CreateObjectExtValue() const;  //create object

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormReport == object->GetTypeForm()) {
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
	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectReport::FillFormObject);

	friend class ibValueRecordDataObjectReport;
	friend class ibMetaData;
};

#define default_meta_id 10 //for reports

class ibValueMetaObjectExternalReport : public ibValueMetaObjectReport {
	public:
	ibValueMetaObjectExternalReport() : ibValueMetaObjectReport() {
		m_metaId = default_meta_id;
	}

	//create from file?
	virtual bool IsExternalCreate() const { return true; }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectReport : public ibValueRecordDataObjectExt {
	public:
	ibValueRecordDataObjectReport(const ibValueRecordDataObjectReport& source);
	ibValueRecordDataObjectReport(const ibValueMetaObjectReport* metaObject);
public:

	// ShowFormValue / GetFormValue inherited from base. Report has a
	// single form-id.
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return ibValueMetaObjectReport::eFormReport;
	}
public:

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& action, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectReport;
	friend class ibValueModuleRuntimeManagerExternalReport;
};

// External report value object: regular report behaviour + RAII ownership of the
// transient external metadata container, dropped in ibExternalOwnerHelper's dtor.
// Embedded / config reports use the plain ibValueRecordDataObjectReport.
class ibValueRecordDataObjectExternalReport :
	public ibValueRecordDataObjectReport,
	public ibExternalOwnerHelper {
public:
	ibValueRecordDataObjectExternalReport(const ibValueMetaObjectReport* metaObject, ibMetaData* ownedMeta = nullptr)
		: ibValueRecordDataObjectReport(metaObject), ibExternalOwnerHelper(ownedMeta) {}
};

#endif
