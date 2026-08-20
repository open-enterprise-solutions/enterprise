#ifndef __REPORT_H__
#define __REPORT_H__

#include "commonObject.h"
#include "backend/metaCollection/metaComposerObject.h"   // the report's own composers

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

	// ⭐ A REPORT HOSTS COMPOSERS, and only a report does. The owner is what decides which kinds it
	// accepts (ResolveChild) — the tree, the copy walkers and the serializer all ask this one
	// question, so a metatype missing from here exists but can never be created under anything.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaComposerCLSID)
			return clsid;
		return ibValueMetaObjectRecordDataExt::ResolveChild(clsid);
	}

	// ⭐ THE COMPOSERS THIS REPORT DECLARES — the report's own children, like its forms and its
	// templates. Declaring one is what makes `Report.<Name>` exist; nobody adds an attribute beside
	// it (Max, 2026-08-20: "an attribute you add by hand; this one lives in the object").
	std::vector<ibValueMetaObjectComposer*> GetComposerArrayObject(
		std::vector<ibValueMetaObjectComposer*> array = std::vector<ibValueMetaObjectComposer*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectComposer>(array, { g_metaComposerCLSID });
		return array;
	}

	template <typename _T1>
	ibValueMetaObjectComposer* FindComposerObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectComposer>(id, { g_metaComposerCLSID });
	}

	// THE DEFAULT COMPOSER — what makes the report a report. The generated form is built from it
	// (a grid over it plus its settings), and the object's Compose command exists BECAUSE it is
	// set: with no default composer there is nothing for the platform to compose, and a button
	// that composes nothing is worse than no button.
	ibMetaID GetDefComposer() const {
		return m_propertyDefComposer->GetValueAsInteger();
	}

	void SetDefComposer(const ibMetaID& id) const {
		m_propertyDefComposer->SetValue(id);
	}

	// The FIRST composer declared becomes the default one, and removing the default one clears it —
	// the same two hooks a form already has (OnCreateFormObject / OnRemoveMetaForm), so a person
	// never has to know that "default" is a separate property to fill in.
	void OnCreateComposerObject(ibValueMetaObjectComposer* metaComposer);
	void OnRemoveComposerObject(ibValueMetaObjectComposer* metaComposer);

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

	// The default composer is offered from the report's OWN composers — the same shape the default
	// form is chosen with, so the two questions are answered by one kind of control.
	bool FillComposer(ibPropertyList* prop) {
		for (auto object : GetComposerArrayObject()) {
			if (!object->IsAllowed()) continue;
			prop->AppendItem(
				object->GetName(),
				object->GetMetaID(),
				object->GetIcon(),
				object);
		}

		return true;
	}

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectReport::FillFormObject);
	ibPropertyList* m_propertyDefComposer = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultComposer"), _("Default composer"), &ibValueMetaObjectReport::FillComposer);

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

	// ⏳ WHAT WILL BECOME THE MODEL FACE. A report shows what its DEFAULT composer composes, and it
	// speaks FIRST: `Composing` is the author's chance to fill the sheet himself (taking the
	// composer's settings if he wants them), after which the platform stands down. These two are
	// already shaped like ibValueSpreadsheetModel's verbs — a report cannot DERIVE it until the
	// composer comes off the cursor model, because a report object already carries the runtime once.
	BACKEND_API bool Compose(class ibBackendSpreadsheetObject* document);
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

	// ⭐ THE OBJECT CARRIES ITS COMPOSITIONS. Each composer the report declares becomes a live
	// composition on the object, seeded from what the metaobject holds — which is the DEFAULT of the
	// user's settings, not a separate author's copy. `Report.<Name>` reaches any of them, and
	// `Report.Composer` the default one (the SAME object, not a second copy: two names, one value,
	// or a filter set through one of them would be invisible through the other).
	// BACKEND_API on the methods, not the class: these two are what the FRONTEND reaches (the
	// gridbox resolves a report object to its default composer), and the class itself carries
	// members no export boundary needs to see.
	BACKEND_API ibValueDataComposition* GetComposition(const ibMetaID& id) const;
	BACKEND_API ibValueDataComposition* GetDefaultComposition() const;

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& action, ibBackendValueForm* srcForm);

	// Run the report: calls the object module's Composing(StandartProcessing) handler — the
	// report's twin of a document's Posting. COMPOSE, not generate: Generation in this tree
	// means entering an object on the basis of another. The flag arrives TRUE ("the platform
	// composes"); a handler that composed the result itself sets it FALSE. See
	// dataReportAction.cpp.
	// BACKEND_API: the gridbox's Generate raises this before composing anything itself.
	BACKEND_API bool Composing() const;

protected:
	// The platform side of that flag — virtual so a report kind can own its standard
	// composition. Returns false today: a standard composition needs a declared composition
	// schema on the metaobject, and the Report metaobject has none yet (see the long note in
	// dataReportAction.cpp). This is where that execution lands when the schema arrives.
	virtual bool DoStandardCompose() const;

public:

	// WHAT THE OBJECT PUBLISHES — its attributes and tables (the base) plus its COMPOSITIONS by
	// name, and `Composer` for the default one.
	void FillDataMembers(class ibMemberTable& helper) const;

	// …and what a FORM sees of it. The composers are nodes here too, which is what puts a gridbox
	// on the generated form without anybody drawing one (Max, 2026-08-20: "the form is not made —
	// there is a composer, so no form has to be made").
	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	// A report's MAIN node is its DEFAULT composer — the one a generated form is built from, and the
	// one whose control speaks for the whole form.
	virtual bool IsMainSourceNode(const ibMetaID& id) const override;

	// (GetValueByMetaID is NOT overridden: a composition is a FIELD of the object, so the base finds
	//  it where it finds every other one.)
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) override;

protected:

	// ⭐ WHERE A COMPOSITION IS BORN — the object's own filler, beside the attributes and the tabular
	// sections, into the same store (Max, 2026-08-20: "it is created, initialised, the whole cycle,
	// like a tabular section"). No map of its own: a second store beside m_listObjectValue would be
	// a second lifecycle, and the two would part company at the first re-read.
	virtual void PrepareEmptyObject() override;

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
