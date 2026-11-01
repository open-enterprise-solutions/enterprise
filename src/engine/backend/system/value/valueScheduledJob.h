#ifndef __VALUE_SCHEDULED_JOB_H__
#define __VALUE_SCHEDULED_JOB_H__

// ScheduledJob — ONE scheduled job as a script holds it, whichever kind it is.
//
// The configuration declares two kinds (globalContextManager.cpp, ScheduledJobs): a PREDEFINED job is one
// metaobject with its live settings in sys_job, a PARAMETERIZED one is a row of its metaobject's table. A
// script that lists jobs, switches one off or moves its schedule asks the same questions of both, so both
// answer through this one value — and each keeps its settings where they already live:
//
//   predefined     — the JobSettings row (valueJob.h): Write stores it in sys_job and applies it at once.
//   parameterized  — the row's object: Write writes the row, with its handlers, as its card does.
//
//   Job = ScheduledJobs.FindPredefined(Metadata.ScheduledJobs.Cleanup);
//   Job.Use = False;
//   Job.Write();
//
//   Row = ScheduledJobs.CreateScheduledJob(Metadata.ParameterizedJobs.Exchange);
//   Row.Description = "Warehouse 2";
//   Row.Parameters = New Structure("Node", Warehouse2);
//   Row.Write();
//
// What differs is said by refusing, never by a quiet no-op: a predefined job has no parameters, its key is
// its name, and it cannot be deleted — it is declared, not stored.

#include "backend/compiler/value.h"
#include "valueJob.h"

class ibMetaData;
class ibValueMetaObject;
class ibValueMetaObjectScheduledJob;
class ibValueMetaObjectParameterizedJob;
class ibValueRecordDataObjectHierarchyRef;

void ibValueScheduledJob_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueScheduledJob : public ibValueStaticMembers<&ibValueScheduledJob_BindNames> {
public:

	ibValueScheduledJob();
	// A predefined job — its settings as the base holds them, or as it declares them while the base holds none.
	explicit ibValueScheduledJob(const ibValueMetaObjectScheduledJob* job);
	// A row of a parameterized job — a loaded one, or a new one that is stored by Write.
	ibValueScheduledJob(const ibValueMetaObjectParameterizedJob* kind,
		const ibValuePtr<ibValueRecordDataObjectHierarchyRef>& row);
	virtual ~ibValueScheduledJob();

	virtual ibString GetString() const override;
	virtual bool IsEmpty() const override { return m_predefined == nullptr && m_kind == nullptr; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

	// THE MANAGER'S VERBS — ScheduledJobs.<verb>, each over the configuration it is given.
	//
	// GetScheduledJobs — every job, predefined first, then each parameterized kind's rows; the filter is a
	//   Structure of Metadata, Predefined, Use, Key, Description, UUID (equal-to).
	// FindPredefined   — the predefined job of a metaobject; a parameterized one is refused.
	// FindByUUID       — a predefined job by its metaobject's UUID or a row by its own; Undefined if none.
	// CreateScheduledJob — a new row of a parameterized kind; a predefined one is refused, it exists once.
	static ibValue GetScheduledJobs(ibMetaData* metaData, const ibValue& filter);
	static ibValue FindPredefined(ibMetaData* metaData, const ibValue& metadata);
	static ibValue FindByUUID(ibMetaData* metaData, const ibValue& uuid);
	static ibValue CreateScheduledJob(ibMetaData* metaData, const ibValue& metadata);

private:

	bool IsPredefined() const { return m_predefined != nullptr; }
	ibGuid GetKeyGuid() const;
	ibString GetName() const;

	// A row's value by one of its own attributes, and the same for a write.
	ibValue RowValue(const class ibValueMetaObjectAttributeBase* attribute) const;
	void SetRowValue(const class ibValueMetaObjectAttributeBase* attribute, const ibValue& value);

	ibValue GetParameters() const;
	void SetParameters(const ibValue& parameters);
	ibValue GetLastJob() const;

	// One of the two is set, never both.
	const ibValueMetaObjectScheduledJob*                   m_predefined = nullptr;
	ibValuePtr<ibValuePredefinedJobs::ibValueJobRow>       m_settings;

	const ibValueMetaObjectParameterizedJob*               m_kind = nullptr;
	ibValuePtr<ibValueRecordDataObjectHierarchyRef>        m_row;
};

constexpr ibClassID g_valueScheduledJobCLSID = system_to_clsid("VL_SCJOB");

#endif // !__VALUE_SCHEDULED_JOB_H__
