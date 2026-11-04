////////////////////////////////////////////////////////////////////////////
//	Description : ScheduledJob — one scheduled job, predefined or a parameterized row
////////////////////////////////////////////////////////////////////////////

#include "valueScheduledJob.h"

#include "backend/appData.h"                                   // ibApplicationInstance::GetJobManager
#include "backend/backend_exception.h"
#include "backend/job/jobManager.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaScheduledJobObject.h"
#include "backend/metaCollection/partial/parameterizedJob.h"
#include "backend/query/dataQueryBuilder.h"                    // the rows of a parameterized kind
#include "backend/system/value/valueBackgroundJob.h"           // LastJob
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueMap.h"                     // Parameters and the filter

#include <algorithm>

namespace {

enum Prop {
	enUse = 0,
	enSchedule,
	enParameters,
	enKey,
	enDescription,
	enLastJob,
	enLastRun,
	enUUID,
	enMetadata,
	enPredefined,
};

enum Method {
	enWrite = 0,
	enDelete,
};

// THE METAOBJECT A SCRIPT NAMES — `Metadata.ScheduledJobs.X`, or its name. Either kind; which one the
// caller wanted is the verb's question, asked by the caller.
const ibValueMetaObject* JobMetaObject(ibMetaData* metaData, const ibValue& metadata, const ibString& verb)
{
	ibValueMetaObject* metaObject = nullptr;
	if (metadata.ConvertToValue(metaObject) && metaObject != nullptr) {
		const ibClassID clsid = metaObject->GetClassType();
		if (clsid == g_metaScheduledJobCLSID || clsid == g_metaParameterizedJobCLSID)
			return metaObject;
		ibBackendCoreException::Error(_("%s: '%s' is not a scheduled job"), verb, metaObject->GetName());
	}
	if (metadata.GetType() == ibValueTypes::TYPE_STRING && metaData != nullptr) {
		const ibString name = metadata.GetString();
		if (const ibValueMetaObject* job = metaData->FindAnyObjectByFilter<ibValueMetaObject>(name, g_metaScheduledJobCLSID))
			return job;
		if (const ibValueMetaObject* kind = metaData->FindAnyObjectByFilter<ibValueMetaObject>(name, g_metaParameterizedJobCLSID))
			return kind;
	}
	ibBackendCoreException::Error(_("%s: '%s' is not a scheduled job of this configuration"), verb, metadata.GetString());
	return nullptr;
}

// THE ROWS OF ONE KIND — every item, folders left out (a folder is not a job), in the table's order.
std::vector<ibGuid> RowsOf(const ibValueMetaObjectParameterizedJob* kind)
{
	std::vector<ibGuid> rows;
	const ibBackendQueryable* const queryable = kind->GetQueryable();
	if (queryable == nullptr)
		return rows;

	ibDataQueryBuilder query;
	query.From(queryable).Where(kind->GetDataIsFolder()->GetQueryColumn(), ibValue(false));

	ibReadPageRequest page;
	page.m_count = 0;
	ibDataQueryResult selection = query.Execute(page);
	while (selection.Next()) {
		// Held by a local while it is read — parameterizedJobRegistration.cpp says why.
		const ibValue rowValue = selection.GetValue(kind->GetDataReference()->GetQueryColumn());
		ibValueReferenceDataObject* reference = nullptr;
		if (!rowValue.ConvertToValue(reference) || reference == nullptr)
			continue;
		const ibGuid rowGuid = reference->GetGuid().GetGuid();
		if (rowGuid.isValid())
			rows.push_back(rowGuid);
	}
	return rows;
}

ibValuePtr<ibValueScheduledJob> LoadRow(const ibValueMetaObjectParameterizedJob* kind, const ibGuid& rowGuid)
{
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> row(kind->CreateObjectValue(ibObjectMode::OBJECT_ITEM, rowGuid));
	if (row == nullptr)
		ibBackendCoreException::Error(_("the job row '%s' of '%s' could not be read"), rowGuid.str(), kind->GetName());
	return ibValuePtr<ibValueScheduledJob>(new ibValueScheduledJob(kind, row));
}

std::vector<const ibValueMetaObjectScheduledJob*> PredefinedJobs(ibMetaData* metaData)
{
	std::vector<const ibValueMetaObjectScheduledJob*> jobs;
	for (const auto object : metaData->GetAnyArrayObject(g_metaScheduledJobCLSID))
		if (!object->IsDeleted())
			if (const auto job = dynamic_cast<const ibValueMetaObjectScheduledJob*>(object))
				jobs.push_back(job);
	return jobs;
}

std::vector<const ibValueMetaObjectParameterizedJob*> ParameterizedKinds(ibMetaData* metaData)
{
	std::vector<const ibValueMetaObjectParameterizedJob*> kinds;
	for (const auto object : metaData->GetAnyArrayObject(g_metaParameterizedJobCLSID))
		if (!object->IsDeleted())
			if (const auto kind = dynamic_cast<const ibValueMetaObjectParameterizedJob*>(object))
				kinds.push_back(kind);
	return kinds;
}

ibGuid UUIDOf(const ibValue& value, const ibString& verb)
{
	ibValueGuid* uuid = nullptr;
	if (value.ConvertToValue(uuid) && uuid != nullptr)
		return *uuid;
	ibGuid parsed;
	if (value.GetType() == ibValueTypes::TYPE_STRING && ibGuid::parse(value.GetString(), parsed))
		return parsed;
	ibBackendCoreException::Error(_("%s: '%s' is not a UUID"), verb, value.GetString());
	return ibGuid();
}

ibMetaData* RequireMetaData(ibMetaData* metaData)
{
	if (metaData == nullptr)
		ibBackendCoreException::Error(_("Scheduled jobs: no configuration is open"));
	return metaData;
}

} // namespace

//////////////////////////////////////////////////////////////////////
//                             the job                              //
//////////////////////////////////////////////////////////////////////

void ibValueScheduledJob_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Use"));
	helper.AppendProp(ibT("Schedule"));
	helper.AppendProp(ibT("Parameters"));
	helper.AppendProp(ibT("Key"));
	helper.AppendProp(ibT("Description"));
	helper.AppendProp(ibT("LastJob"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("LastRun"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("UUID"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Metadata"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Predefined"), true, false, wxNOT_FOUND);

	helper.AppendProc(ibT("Write"), ibT("Write()"));
	helper.AppendProc(ibT("Delete"), ibT("Delete()"));
}

ibValueScheduledJob::ibValueScheduledJob()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueScheduledJob::ibValueScheduledJob(const ibValueMetaObjectScheduledJob* job)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_predefined(job)
{
	m_settings = ibValuePtr<ibValuePredefinedJobs::ibValueJobRow>(
		new ibValuePredefinedJobs::ibValueJobRow(job->GetGuid(), job->GetName(), job->IsUsed(), job->GetSchedule()));
}

ibValueScheduledJob::ibValueScheduledJob(const ibValueMetaObjectParameterizedJob* kind,
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef>& row)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_kind(kind), m_row(row)
{
}

ibValueScheduledJob::~ibValueScheduledJob()
{
}

ibGuid ibValueScheduledJob::GetKeyGuid() const
{
	if (IsPredefined())
		return m_predefined->GetGuid();
	return m_row != nullptr ? m_row->GetGuid().GetGuid() : ibGuid();
}

ibString ibValueScheduledJob::GetName() const
{
	if (IsPredefined())
		return m_predefined->GetName();
	return m_kind != nullptr ? m_kind->GetName() : ibString();
}

ibString ibValueScheduledJob::GetString() const
{
	if (IsPredefined())
		return m_predefined->GetSynonym();
	if (m_kind == nullptr || m_row == nullptr)
		return ibT("ScheduledJob");
	const ibString description = RowValue(m_kind->GetDataDescription()).GetString();
	return description.IsEmpty() ? m_kind->GetSynonym() : description;
}

ibValue ibValueScheduledJob::RowValue(const ibValueMetaObjectAttributeBase* attribute) const
{
	ibValue value;
	m_row->GetValueByMetaID(attribute->GetMetaID(), value);
	return value;
}

void ibValueScheduledJob::SetRowValue(const ibValueMetaObjectAttributeBase* attribute, const ibValue& value)
{
	if (!m_row->SetValueByMetaID(attribute->GetMetaID(), value))
		ibBackendCoreException::Error(_("the job row of '%s' did not take '%s'"), m_kind->GetName(), attribute->GetName());
}

ibValue ibValueScheduledJob::GetParameters() const
{
	// A ROW'S PARAMETERS ARE ITS OWN ATTRIBUTES — the ones its kind declares, by name. The job's handler reads
	// them off the row it is given, so there is nothing else a parameter could be. A predefined job has none.
	ibValuePtr<ibValueStructure> parameters(new ibValueStructure());
	if (!IsPredefined())
		for (const ibValueMetaObjectAttributeBase* attribute : m_kind->GetAttributeArrayObject())
			parameters->Insert(attribute->GetName(), RowValue(attribute));
	return parameters;
}

void ibValueScheduledJob::SetParameters(const ibValue& parameters)
{
	ibValueStructure* structure = nullptr;
	const bool given = !parameters.IsEmpty();
	if (given && (!parameters.ConvertToValue(structure) || structure == nullptr))
		ibBackendCoreException::Error(_("the parameters of '%s' are its attributes: give them as a Structure"), GetName());

	if (IsPredefined()) {
		if (structure != nullptr && structure->Count() > 0)
			ibBackendCoreException::Error(_("'%s' is a predefined job: it takes no parameters"), GetName());
		return;
	}
	if (structure == nullptr)
		return;

	// Every name must be an attribute of the kind: a parameter nobody reads is a typo, and setting the
	// others while it is dropped would start the job with half of what was meant.
	const std::vector<ibValueMetaObjectAttributeBase*> attributes = m_kind->GetAttributeArrayObject();
	for (const ibValueContainer::ibContainerEntry& entry : structure->Entries()) {
		const ibString name = entry.key.GetString();
		const auto found = std::find_if(attributes.begin(), attributes.end(),
			[&name](const ibValueMetaObjectAttributeBase* a) { return stringUtils::IsSameAs(a->GetName(), name); });
		if (found == attributes.end())
			ibBackendCoreException::Error(_("'%s' has no attribute '%s' to take it as a parameter"), m_kind->GetName(), name);
		SetRowValue(*found, entry.value);
	}
}

ibValue ibValueScheduledJob::GetLastJob() const
{
	// THE LAST RUN THIS APPLICATION SAW — the manager keeps it as an outcome, and it is handed over as the
	// BackgroundJob a run is, so State / Begin / End / ErrorInfo are asked the same way of both. A job that has
	// not run here (or that a peer ran instead — a skip) answers Undefined; when it last ran anywhere is
	// LastRun.
	ibJobManager* const manager = ibApplicationInstance::GetJobManager();
	if (manager == nullptr)
		return ibValue();

	const ibGuid key = GetKeyGuid();
	for (const ibJobState& state : manager->Snapshot()) {
		if (!(state.m_key == key))
			continue;

		ibBackgroundJobState runState;
		if (state.m_running)
			runState = ibBackgroundJobState_Active;
		else if (state.m_outcome == ibJobOutcome::Succeeded)
			runState = ibBackgroundJobState_Completed;
		else if (state.m_outcome == ibJobOutcome::Failed)
			runState = ibBackgroundJobState_Failed;
		else if (state.m_outcome == ibJobOutcome::Cancelled)
			runState = ibBackgroundJobState_Canceled;
		else
			return ibValue();

		ibBackgroundLabel label;
		label.m_method = state.m_name;
		label.m_key = key.str();
		label.m_description = GetString();
		return ibValue(new ibValueBackgroundJob(ibBackgroundRun::Recorded(label,
			state.m_lastRunAt, state.m_lastEndAt, runState, state.m_error)));
	}
	return ibValue();
}

bool ibValueScheduledJob::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (IsEmpty())
		return false;

	switch (lPropNum) {
	case enUse:
		pvarPropVal = IsPredefined() ? ibValue(m_settings->IsActive()) : RowValue(m_kind->GetDataActive());
		return true;
	case enSchedule:
		// A COPY for a predefined job, as JobSettings hands it: read it, change it, assign it back.
		pvarPropVal = IsPredefined() ? ibValue(new ibValueSchedule(m_settings->GetSchedule()))
		                             : RowValue(m_kind->GetDataSchedule());
		return true;
	case enParameters:
		pvarPropVal = GetParameters();
		return true;
	case enKey:
		// A predefined job's key is its name; a row's is its code.
		pvarPropVal = IsPredefined() ? ibValue(m_predefined->GetName()) : RowValue(m_kind->GetDataCode());
		return true;
	case enDescription:
		pvarPropVal = IsPredefined() ? ibValue(m_predefined->GetSynonym()) : RowValue(m_kind->GetDataDescription());
		return true;
	case enLastJob:
		pvarPropVal = GetLastJob();
		return true;
	case enLastRun:
		// When it last ran on this base, by any process — the shared clock (sys_job). Never run: the empty date.
		pvarPropVal = IsPredefined() ? ibValue(m_settings->GetLastRun())
		                             : ibValue(ibJobManager::ReadSharedLastRun(GetKeyGuid()));
		return true;
	case enUUID:
		pvarPropVal = new ibValueGuid(GetKeyGuid());
		return true;
	case enMetadata:
		pvarPropVal = IsPredefined() ? ibValue(const_cast<ibValueMetaObjectScheduledJob*>(m_predefined))
		                             : ibValue(const_cast<ibValueMetaObjectParameterizedJob*>(m_kind));
		return true;
	case enPredefined:
		pvarPropVal = IsPredefined();
		return true;
	}
	return false;
}

bool ibValueScheduledJob::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (IsEmpty())
		return false;

	switch (lPropNum) {
	case enUse:
		if (IsPredefined()) m_settings->SetActive(varPropVal.GetBoolean());
		else SetRowValue(m_kind->GetDataActive(), ibValue(varPropVal.GetBoolean()));
		return true;
	case enSchedule:
	{
		ibValueSchedule* schedule = nullptr;
		if (!varPropVal.ConvertToValue(schedule) || schedule == nullptr)
			ibBackendCoreException::Error(_("the schedule of '%s' must be a JobSchedule, not a '%s'"),
				GetName(), varPropVal.GetClassName());
		if (IsPredefined()) m_settings->SetSchedule(schedule->GetSchedule());
		else SetRowValue(m_kind->GetDataSchedule(), ibValue(new ibValueSchedule(schedule->GetSchedule())));
		return true;
	}
	case enParameters:
		SetParameters(varPropVal);
		return true;
	case enKey:
		if (IsPredefined())
			ibBackendCoreException::Error(_("'%s' is a predefined job: its key is its name"), GetName());
		SetRowValue(m_kind->GetDataCode(), varPropVal);
		return true;
	case enDescription:
		if (IsPredefined())
			ibBackendCoreException::Error(_("'%s' is a predefined job: its description is its synonym, changed in the designer"), GetName());
		SetRowValue(m_kind->GetDataDescription(), varPropVal);
		return true;
	}
	return false;
}

bool ibValueScheduledJob::CallAsProc(const long lMethodNum, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	if (IsEmpty())
		return false;

	switch (lMethodNum) {
	case enWrite:
		if (IsPredefined()) {
			m_settings->WriteSettings();
			return true;
		}
		// The row's own write — its handlers, its rights, and its registration with the schedule after it.
		if (!m_row->WriteObject())
			ibBackendCoreException::Error(_("the job row '%s' of '%s' was not written"), GetString(), m_kind->GetName());
		return true;
	case enDelete:
		if (IsPredefined())
			ibBackendCoreException::Error(_("'%s' is a predefined job: it is declared, not stored, and cannot be deleted - switch it off with Use"), GetName());
		if (m_row->IsNewObject())
			ibBackendCoreException::Error(_("the job row of '%s' was never written: there is nothing to delete"), m_kind->GetName());
		if (!m_row->DeleteObject())
			ibBackendCoreException::Error(_("the job row '%s' of '%s' was not deleted"), GetString(), m_kind->GetName());
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
//                          the manager's verbs                     //
//////////////////////////////////////////////////////////////////////

ibValue ibValueScheduledJob::GetScheduledJobs(ibMetaData* metaData, const ibValue& filter)
{
	RequireMetaData(metaData);

	ibValueStructure* structure = nullptr;
	if (!filter.IsEmpty() && (!filter.ConvertToValue(structure) || structure == nullptr))
		ibBackendCoreException::Error(_("GetScheduledJobs: the filter must be a Structure"));

	// The filter's fields, read once. A field that names nothing is refused with its name: a filter that
	// ignored a misspelt key would answer with every job there is.
	static const ibString kFields[] = { ibT("Metadata"), ibT("Predefined"), ibT("Use"), ibT("Key"), ibT("Description"), ibT("UUID") };
	std::vector<std::pair<long, ibValue>> wanted;
	if (structure != nullptr) {
		for (const ibValueContainer::ibContainerEntry& entry : structure->Entries()) {
			const ibString name = entry.key.GetString();
			if (std::none_of(std::begin(kFields), std::end(kFields), [&name](const ibString& f) { return stringUtils::IsSameAs(f, name); }))
				ibBackendCoreException::Error(
					_("GetScheduledJobs: the filter has no field '%s' (Metadata, Predefined, Use, Key, Description, UUID)"), name);
			ibValue value = entry.value;
			if (stringUtils::IsSameAs(name, ibT("UUID")))
				value = new ibValueGuid(UUIDOf(entry.value, ibT("GetScheduledJobs")));
			// The member's number on a job — the same table every job answers with.
			const ibValuePtr<ibValueScheduledJob> probe(new ibValueScheduledJob());
			wanted.emplace_back(probe->FindProp(name), value);
		}
	}

	std::vector<ibValuePtr<ibValueScheduledJob>> candidates;
	for (const ibValueMetaObjectScheduledJob* job : PredefinedJobs(metaData))
		candidates.emplace_back(new ibValueScheduledJob(job));
	for (const ibValueMetaObjectParameterizedJob* kind : ParameterizedKinds(metaData))
		for (const ibGuid& rowGuid : RowsOf(kind))
			candidates.push_back(LoadRow(kind, rowGuid));

	ibValuePtr<ibValueArray> found(new ibValueArray());
	for (const ibValuePtr<ibValueScheduledJob>& job : candidates) {
		bool match = true;
		for (const auto& [prop, value] : wanted) {
			ibValue has;
			job->GetPropVal(prop, has);
			if (!(has == value)) { match = false; break; }
		}
		if (match)
			found->Add(ibValue(job));
	}
	return found;
}

ibValue ibValueScheduledJob::FindPredefined(ibMetaData* metaData, const ibValue& metadata)
{
	const ibValueMetaObject* const metaObject = JobMetaObject(RequireMetaData(metaData), metadata, ibT("FindPredefined"));
	const auto job = dynamic_cast<const ibValueMetaObjectScheduledJob*>(metaObject);
	if (job == nullptr)
		ibBackendCoreException::Error(
			_("FindPredefined: '%s' is a parameterized job - its jobs are rows, found with GetScheduledJobs or FindByUUID"),
			metaObject->GetName());
	return ibValue(new ibValueScheduledJob(job));
}

ibValue ibValueScheduledJob::FindByUUID(ibMetaData* metaData, const ibValue& uuid)
{
	RequireMetaData(metaData);
	const ibGuid key = UUIDOf(uuid, ibT("FindByUUID"));

	for (const ibValueMetaObjectScheduledJob* job : PredefinedJobs(metaData))
		if (job->GetGuid() == key)
			return ibValue(new ibValueScheduledJob(job));
	for (const ibValueMetaObjectParameterizedJob* kind : ParameterizedKinds(metaData))
		for (const ibGuid& rowGuid : RowsOf(kind))
			if (rowGuid == key)
				return ibValue(LoadRow(kind, rowGuid));

	// Not found is an answer, not an error: the row may have been deleted since the UUID was kept.
	return ibValue();
}

ibValue ibValueScheduledJob::CreateScheduledJob(ibMetaData* metaData, const ibValue& metadata)
{
	const ibValueMetaObject* const metaObject = JobMetaObject(RequireMetaData(metaData), metadata, ibT("CreateScheduledJob"));
	const auto kind = dynamic_cast<const ibValueMetaObjectParameterizedJob*>(metaObject);
	if (kind == nullptr)
		ibBackendCoreException::Error(
			_("CreateScheduledJob: '%s' is a predefined job - it exists once, find it with FindPredefined"),
			metaObject->GetName());

	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> row(kind->CreateObjectValue(ibObjectMode::OBJECT_ITEM));
	if (row == nullptr)
		ibBackendCoreException::Error(_("CreateScheduledJob: a row of '%s' could not be created"), kind->GetName());
	return ibValue(new ibValueScheduledJob(kind, row));
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// VENDED: a job is found or created through ScheduledJobs, never with New.
SYSTEM_TYPE_REGISTER(ibValueScheduledJob, "ScheduledJob", g_valueScheduledJobCLSID);
