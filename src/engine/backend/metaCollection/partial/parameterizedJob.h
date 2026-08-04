#ifndef __PARAMETERIZED_JOB_H__
#define __PARAMETERIZED_JOB_H__

// A PARAMETERIZED scheduled job — unattended work whose unit of multiplication is a ROW, not a
// declaration. The code is written once; the instances are data (docs/scheduled-jobs.md § 3).
//
// Read it as a catalog entry that, next to Write, also has EXECUTE. That is why it derives from the
// hierarchical record base rather than owning some structure of its own: description, folders,
// deletion mark, forms and the whole list machinery arrive already written, and grouping in the
// list is the same declaration that gives grouping in a report. What it does NOT take is the
// catalog's OWNER — a job serves the data, it is not subordinate to another list.
//
// It carries four requisites of its own, and each is a column because each is asked about in a
// query rather than computed per row:
//
//   Active   — this row's own on/off. A parallel to the deletion mark: switching a misbehaving
//              exchange off must not require the Designer, and must not delete anything.
//   Schedule — a JobSchedule value (system/value/valueJob.h), stored whole in its own blob.
//   LastRun  — when it last finished. The schedule is a pure function of (last run, now), so the
//              row needs its own. This one IS stored: it is a fact about the past.
//   NextRun  — DERIVED, and deliberately never stored. It is a pure function of (schedule, last
//              run) computed at the moment it is read, so it cannot go stale after a restart, a
//              clock change or a schedule edit — the failure mode a stored copy has. The column
//              therefore stays EMPTY in the database and a query over it reads nothing; the value
//              exists on the OBJECT, where the requisite is read.
//
// MODULES — two, and the split is not arbitrary (§ 5):
//   * the MANAGER module holds the execution — `Procedure JobProcessing(Job)`, taking the row's
//     own reference. The scheduler knows the METAOBJECT, never a row, so the entry point is a
//     type-level operation and the reference is its argument. It is also the same handler NAME the
//     predefined kind uses, where it takes no argument because there is only ever one of it.
//   * the OBJECT module holds the write logic (BeforeWrite / OnWrite / …), exactly as a catalog's.
//
// The argument is the ONLY one: everything a job needs it reads off itself, on its own side, under
// its own rights — which is also what the value gate already requires, since a loaded object does
// not cross a session boundary but a reference always travels.

#include "commonObject.h"
#include "reference/reference.h"

#include "backend/job/jobSchedule.h"
#include "backend/propertyManager/property/propertySchedule.h"   // the designer-side default schedule
#include "backend/system/value/valueJob.h"                        // g_valueScheduleCLSID — the Schedule requisite's type

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectParameterizedJob : public ibValueMetaObjectRecordDataHierarchyMutableRef {
	public:
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19300,
		ID_METATREE_OPEN_MANAGER = 19301,
		ID_METATREE_RUN_JOB = 19302,
	};

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect,
		eFormFolder,
		eFormFolderSelect
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormObject"), _("Form object"), eFormObject);
		formList.AppendItem(wxT("FormFolder"), _("Form group"), eFormFolder);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		formList.AppendItem(wxT("FormSelect"), _("Form select"), eFormSelect);
		formList.AppendItem(wxT("FormGroupSelect"), _("Form group select"), eFormFolderSelect);
		return formList;
	}

public:

	// The row's own requisites — the four columns the scheduler and the card both read.
	ibValueMetaObjectAttributePredefined* GetDataActive() const { return m_propertyAttributeActive->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDataSchedule() const { return m_propertyAttributeSchedule->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDataLastRun() const { return m_propertyAttributeLastRun->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDataNextRun() const { return m_propertyAttributeNextRun->GetMetaObject(); }

	// WHEN a NEW row is due — the metadata's schedule is the DEFAULT a fresh row starts from, never
	// the live value: the live one lives in the row, which is the whole point of the parameterized
	// kind (§ 8).
	ibJobScheduleDescription& GetDefaultSchedule() const { return m_propertySchedule->GetValueAsSchedule(); }

	// Declared but switched off — the whole metaobject, rows and all. Distinct from a row's own
	// Active: this one is the developer's switch, that one is the administrator's.
	bool IsUsed() const { return m_propertyUse->GetValueAsBoolean(); }

	// The name the job is registered under with ibJobManager — and therefore the key of its
	// cross-process claim (sys_lock, `Job.<name>`) and of its shared clock (sys_job). ONE
	// registration per metaobject, never one per row: a registration holds a session, a session
	// owns a connection, and a hundred and fifty exchanges would exhaust the pool before the first
	// one ran (§ 7). The body iterates the due rows instead.
	wxString GetJobName() const { return GetName(); }

	// RUNNING IS ITS OWN RIGHT, beside the Read / Write / Delete triplet a catalog brings. The two
	// are genuinely different jobs of work: an operator may be trusted to fire an exchange and not
	// to change what it exchanges, and — the other way round — an administrator may edit the
	// settings of jobs they never run by hand. Nothing here implies anything about Write: the
	// after-run stamp is written privileged for exactly that reason.
	bool AccessRight_Execute() const { return IsFullAccess() || AccessRight(m_roleExecute); }

	//default constructor
	ibValueMetaObjectParameterizedJob();
	virtual ~ibValueMetaObjectParameterizedJob();

	// This source's OWN command, on top of the inherited writeable + folder set. The shape a
	// document's Post already has: the list command loads the row by key and runs it.
	enum {
		eExecuteValue = 28,
	};

	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

	// CALL THE HANDLER for one row — resolve the manager module and run JobProcessing(Ref). The one
	// entry every initiator goes through; none of them brings a mechanism of its own.
	bool RunHandler(const ibGuid& objGuid) const;

	// Record that it ran, on the object the CALLER already holds. Privileged, so "may run" does not
	// silently require "may write".
	//
	// WHOSE object matters. A row's version is per loaded object, so stamping a second copy of a
	// row that a card also has open bumps the version behind that card's back, and its next save is
	// refused as "changed by another user". Whoever has an object passes THAT one.
	void StampLastRun(const ibGuid& objGuid) const;

	// RUN ONE ROW, now, ignoring its schedule — handler plus stamp, for the initiators that hold no
	// object of their own: the tick, the list command, script. A card runs the two halves itself
	// against the object it already has.
	bool RunJobByReference(const ibGuid& objGuid) const;

	// WHEN THIS ROW IS NEXT DUE — computed from (schedule, last run) every time it is asked, never
	// stored. NOT a second scheduler: it composes the SAME pure rules the manager asks
	// (ibJobScheduleRules), so a row and the tick can never disagree about what its calendar means.
	static wxDateTime ComputeNextRun(const ibJobScheduleDescription& schedule, const wxDateTime& lastRun);

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

	//get attribute code
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const {
		return m_propertyAttributeCode->GetMetaObject();
	}

	//create associate value
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
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

	// Additive contract — chains to HierarchyMutableRef (Reference, DeletionMark, DataVersion,
	// PredefinedName, Code, Description, Parent, IsFolder) and appends the four the job adds.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataHierarchyMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeActive->GetMetaObject());
		array.push_back(m_propertyAttributeSchedule->GetMetaObject());
		array.push_back(m_propertyAttributeLastRun->GetMetaObject());
		array.push_back(m_propertyAttributeNextRun->GetMetaObject());
		return true;
	}

	//searched array
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
		};
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid = wxNullGuid) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// Declare / withdraw with ibJobManager — no-ops in the Designer (which edits jobs, never runs
	// them) and when there is no manager (launcher, pre-bootstrap).
	//
	// ONE ENTRY PER ACTIVE ROW. A row IS the job: it carries the schedule, the last run and the
	// switch, so it is what the manager watches. RegisterJobs walks the table once, when the
	// configuration starts; a row that is written afterwards re-registers itself (RegisterRow), and
	// one that is switched off or deleted withdraws.
	//
	// Registration costs nothing while a job sleeps — no session, no connection, two timestamp
	// comparisons per tick — and the manager's cap is on CONCURRENT RUNS, not on entries, so the
	// hundred-and-fiftieth exchange is as declarable as the first.
	bool RegisterJobs();
	bool UnregisterJobs();

public:

	// A single row's entry — used by the row itself after it is written, and by the table walk
	// above. Withdraw + declare, so an edited schedule takes effect at once.
	bool RegisterRow(const ibGuid& objGuid, bool active, const ibJobScheduleDescription& schedule,
		const wxString& presentation) const;
	bool UnregisterRow(const ibGuid& objGuid, bool forgetState = false) const;

	// The name a row is registered under: the job's name plus the row's guid. The guid is what
	// makes it unique — two rows may perfectly well share a description — and the name in front is
	// what makes the journal and Active Users readable.
	wxString GetRowJobName(const ibGuid& objGuid) const;

protected:


private:

	bool FillFormObject(ibPropertyList* prop) { return FillFormByType(prop, eFormObject); }
	bool FillFormFolder(ibPropertyList* prop) { return FillFormByType(prop, eFormFolder); }
	bool FillFormList(ibPropertyList* prop) { return FillFormByType(prop, eFormList); }
	bool FillFormSelect(ibPropertyList* prop) { return FillFormByType(prop, eFormSelect); }
	bool FillFormFolderSelect(ibPropertyList* prop) { return FillFormByType(prop, eFormFolderSelect); }

	// The five default-form lists differ by ONE constant, so they are one function and five
	// callers rather than five copies of the same loop.
	bool FillFormByType(ibPropertyList* prop, int formType) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (formType == object->GetTypeForm())
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectParameterizedJob::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectParameterizedJob::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectParameterizedJob::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectParameterizedJob::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectParameterizedJob::FillFormFolderSelect);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibPropertyCategory* m_categoryJob = ibPropertyObject::CreatePropertyCategory(wxT("Job"), _("Job"));
	ibPropertyBoolean* m_propertyUse = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryJob, wxT("Use"), _("Use"), true);

	// The DEFAULT a new row starts from. The designer declares, the base holds (§ 8) — so a
	// schedule edited here does not reach rows that already exist, which is correct for a default
	// and is why the card can always be opened to change one row's own.
	ibPropertySchedule* m_propertySchedule = ibPropertyObject::CreateProperty<ibPropertySchedule>(m_categoryJob, wxT("Schedule"), _("Schedule"));

	// Retry on failure — the pair the manager reads. Zero attempts (the default) is the honest
	// setting for work that is not safe to repeat.
	ibPropertyInteger* m_propertyRetryCount = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryJob, wxT("RetryCount"), _("Retry count on failure"), 0);
	ibPropertyInteger* m_propertyRetryInterval = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryJob, wxT("RetryInterval"), _("Retry interval on failure"), 10);

	// The row's own columns. Attribute usage is `Items`, the default — so a FOLDER carries none of
	// them and its card shows Description and Parent, like any catalog group. That is a model and
	// form fact, not a storage one: items and folders share one table.
	ibPropertyContainer<>* m_propertyAttributeActive = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("Active"), _("Active"), wxEmptyString, false, true));
	ibPropertyContainer<>* m_propertyAttributeSchedule = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Schedule"), _("Schedule"), wxEmptyString, g_valueScheduleCLSID));
	ibPropertyContainer<>* m_propertyAttributeLastRun = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateDate(wxT("LastRun"), _("Last run"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime));
	// NextRun is DERIVED — the column exists so the requisite has a name, a type and a place on a
	// form, but nothing ever writes it: the value is generated when it is read (see the object's
	// GetValueByMetaID). No index, therefore, and no ordering by it — there is nothing in the
	// column to order by.
	ibPropertyContainer<>* m_propertyAttributeNextRun = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateDate(wxT("NextRun"), _("Next run"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime));

	// "May run it by hand" is deliberately its own right: editing an exchange's settings and
	// firing the exchange are plausibly different roles, and the mechanism for saying so is next
	// door — a command declares its own through CreateRole.
	ibRole* m_roleExecute = ibValueMetaObject::CreateRole(wxT("Execute"), _("Execute"));

	friend class ibValueRecordDataObjectParameterizedJob;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectParameterizedJob : public ibValueRecordDataObjectHierarchyRef {
	public:
	ibValueRecordDataObjectParameterizedJob(const ibValueMetaObjectParameterizedJob* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectParameterizedJob(const ibValueRecordDataObjectParameterizedJob& source);
public:

	// The job's own methods on top of the base data members. Execute is here as well as on the
	// list, because "run this one" is a thing to do to an OBJECT and the list command is only one
	// of its callers.
	void FillMethods(ibMemberTable& helper) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// NextRun is answered HERE, not read off the row: the stored cell is always empty, and the
	// value is a pure function of (schedule, last run) generated at the moment it is asked. One
	// interception, so every reader — the card, script, a control bound to the requisite — gets
	// the same computed answer through the door they already use.
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	// WRITE — the base write, then this row's own entry in the scheduler.
	//
	// A row IS a job, so writing one is what puts it on the schedule, takes it off, or moves it:
	// a fresh row registers, a switched-off one withdraws, an edited schedule replaces. Doing it
	// here rather than on a tick is what lets the tick stay two timestamp comparisons — the
	// alternative is re-reading the table every second to notice an edit somebody made once.
	virtual bool WriteObject() override;

	// DELETE — the row goes, so its entry goes with it. Otherwise the manager would keep waking a
	// job whose row no longer exists.
	virtual bool DeleteObject() override;

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// RUN THIS ROW — resolves the manager module and calls JobProcessing(Ref) with this row's own
	// reference. Ignores the schedule: this is "by hand", and the schedule is what the tick reads.
	//
	// "It is already running" is NOT re-invented here. A run in flight holds the manager's own
	// cross-process claim, and a second starter is refused by it with an exception that says so —
	// the same answer whether the second one is this button, the list command, script or a peer
	// process.
	bool ExecuteJob();

protected:
	virtual ibFormID GetCurrentObjectFormID() const override;
public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectParameterizedJob;
};

#endif // !__PARAMETERIZED_JOB_H__
