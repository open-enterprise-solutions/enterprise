#ifndef __META_SCHEDULED_JOB_OBJECT_H__
#define __META_SCHEDULED_JOB_OBJECT_H__

// A PREDEFINED scheduled job — the configuration's own unattended work, declared once and existing
// once. It serves the CONFIGURATION; a job that serves DATA is the parameterized kind, a reference
// object whose rows are its instances (docs/scheduled-jobs.md § 2).
//
// It is a plain ibValueMetaObject holding ONE module — the MANAGER module, which is where execution
// lives for both kinds. That choice is not stylistic: the scheduler knows the METAOBJECT, never a
// row, so the entry point is a type-level operation. A manager module also registers with the
// module manager (unlike a command's private module), so it is compiled once with the session's
// modules rather than transiently per run.
//
// The handler is
//     Procedure JobProcessing()
// and the name is deliberately the same one the parameterized kind will use, where it takes the
// job's own reference as its single argument.
//
// LIFECYCLE. The module registers itself on run and unregisters on close — this object only
// forwards. What it adds is its own registration with ibJobManager: declared on the AFTER-run phase
// (the resolve half, once the module is registered) and withdrawn on BEFORE-close, inversely. In
// the Designer nothing is registered at all: a designer that ran the configuration's jobs on a
// schedule would execute application code while it is being edited.

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule
#include "backend/propertyManager/property/propertySchedule.h"
#include "backend/job/jobSchedule.h"

class BACKEND_API ibValueMetaObjectScheduledJob : public ibValueMetaObject {
public:

	ibValueMetaObjectScheduledJob(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }

	// The module carrying JobProcessing. A manager module, so it is name-resolvable in a session —
	// which is also what lets the same handler be exercised by hand while debugging.
	const ibValueMetaObjectManagerModule* GetJobModule() const { return m_propertyJobModule->GetMetaObject(); }

	// WHEN it is due. The description is the engine's own (backend/job/jobSchedule.h), so the
	// designer edits exactly the structure the manager evaluates.
	ibJobScheduleDescription& GetSchedule() const { return m_propertySchedule->GetValueAsSchedule(); }

	// Declared but switched off — still visible, never registered. The Designer's value is the
	// STARTING POINT for a base that has not seen this job yet; the live value belongs in the
	// database (docs/scheduled-jobs.md § 8), which is a later step.
	bool IsUsed() const { return m_propertyUse->GetValueAsBoolean(); }

	// WHAT HAPPENS AFTER A FAILURE. Most failures of unattended work are transient — a network
	// blinked, a peer was busy — and waiting out the whole interval for that is wrong: for a nightly
	// job it means tomorrow. Zero attempts (the default) is the honest setting for work that is not
	// safe to repeat.
	int GetRetryCount() const { return m_propertyRetryCount->GetValueAsInteger(); }
	int GetRetryInterval() const { return m_propertyRetryInterval->GetValueAsInteger(); }

	// The name this job is registered under — and therefore the key of its cross-process claim
	// (sys_lock, `Job.<name>`) and of its shared clock (sys_job). A metaobject name cannot contain a
	// dot, so it can never collide with the platform's own jobs (`totals.fold`).
	wxString GetJobName() const { return GetName(); }

	// Hosts no children — a predefined job is code plus a schedule, and admitting forms or commands
	// here would change the designer tree rather than add a feature.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// designer context menu — "Open job module" opens the handler's code editor, exactly as a
	// common module or a command does.
	virtual bool PrepareContextMenu(wxMenu* defaultMenu) override;
	virtual void ProcessCommand(unsigned int id) override;

	//lifecycle — forward to the inner module (its own registration), then our own with the manager
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	// Run it now, ignoring the schedule — the designer / script door. Without it a job can only be
	// exercised by waiting out its interval, which makes "is the job wrong or is the manager wrong?"
	// unanswerable.
	bool RunNow() const;

protected:

	enum { ID_METATREE_OPEN_JOB_MODULE = 19200 };

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// Declare / withdraw with ibJobManager. Both are no-ops in the Designer and when there is no
	// manager (launcher, pre-bootstrap).
	bool RegisterJob();
	bool UnregisterJob();

private:

	ibPropertyCategory* m_categoryJob = ibPropertyObject::CreatePropertyCategory(wxT("Job"), _("Job"));
	ibPropertyBoolean*  m_propertyUse = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryJob, wxT("Use"), _("Use"), true);
	ibPropertySchedule* m_propertySchedule = ibPropertyObject::CreateProperty<ibPropertySchedule>(m_categoryJob, wxT("Schedule"), _("Schedule"));

	// Retry on failure — the pair the manager reads (jobManager.h). Default 0 attempts: repeating
	// work that failed is only safe when the author says it is.
	ibPropertyInteger* m_propertyRetryCount = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryJob, wxT("RetryCount"), _("Retry count on failure"), 0);
	ibPropertyInteger* m_propertyRetryInterval = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryJob, wxT("RetryInterval"), _("Retry interval on failure"), 10);

	// The handler module — a MANAGER module (registered, compiled with the session), unlike a
	// command's private one.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyJobModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
			m_categoryContext, wxT("JobModule"), _("Job module"));

	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));

	friend class ibMetaData;
};

#endif // !__META_SCHEDULED_JOB_OBJECT_H__
