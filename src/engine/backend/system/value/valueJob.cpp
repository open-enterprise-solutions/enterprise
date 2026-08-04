////////////////////////////////////////////////////////////////////////////
//	Description : the scheduled-job subsystem's script values — schedule, jobs, one job
////////////////////////////////////////////////////////////////////////////

#include "valueJob.h"

#include "backend/appData.h"          // ibApplicationData::GetJobManager
#include "backend/job/jobManager.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaObject.h"   // g_metaScheduledJobCLSID

//////////////////////////////////////////////////////////////////////

ibValueSchedule::ibValueSchedule()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_schedule()
{
}

ibValueSchedule::ibValueSchedule(const ibJobScheduleDescription& schedule)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_schedule(schedule)
{
}

bool ibValueSchedule::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return true;

	// One argument = the interval, in seconds. Anything else is a caller who meant a field, and
	// fields are properties — refusing here keeps "New JobSchedule(...)" from growing a positional
	// argument list that has to be kept in step with fourteen members by hand.
	if (paParams[0]->GetType() != ibValueTypes::TYPE_NUMBER)
		return false;

	m_schedule = ibJobScheduleDescription::EverySeconds(paParams[0]->GetInteger());
	return true;
}

void ibValueSchedule_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("JobSchedule(intervalSeconds : number)"));

	helper.AppendProp(wxT("Interval"));
	helper.AppendProp(wxT("StartTime"));
	helper.AppendProp(wxT("EndTime"));
	helper.AppendProp(wxT("StopAfter"));
	helper.AppendProp(wxT("DaysOfWeek"));
	helper.AppendProp(wxT("DaysOfMonth"));
	helper.AppendProp(wxT("DaysOfMonthFromEnd"));
	helper.AppendProp(wxT("Months"));
	helper.AppendProp(wxT("WeekdayOrdinal"));
	helper.AppendProp(wxT("EveryNWeeks"));
	helper.AppendProp(wxT("EveryNMonths"));
	helper.AppendProp(wxT("PeriodAnchor"));
	helper.AppendProp(wxT("ActiveFrom"));
	helper.AppendProp(wxT("ActiveTo"));

	helper.AppendFunc(wxT("Allowed"), wxT("Allowed(moment : date)"));
	helper.AppendFunc(wxT("NextRun"), wxT("NextRun(notBefore : date)"));
	helper.AppendFunc(wxT("Presentation"), wxT("Presentation()"));
	helper.AppendFunc(wxT("IsValid"), wxT("IsValid()"));
}

bool ibValueSchedule::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAllowed:
		// The CALENDAR half only — "has enough time passed" belongs to whoever holds the last run,
		// which is the manager for a predefined job and the row for a parameterized one.
		pvarRetValue = ibJobScheduleRules::IsAllowed(m_schedule,
			lSizeArray > 0 ? paParams[0]->GetDateTime() : wxDateTime::Now());
		return true;
	case enNextRun:
	{
		const wxDateTime next = ibJobScheduleRules::NextAllowedAfter(m_schedule,
			lSizeArray > 0 ? paParams[0]->GetDateTime() : wxDateTime::Now());
		// An invalid answer means the calendar names no moment within a year (February 31st). It
		// travels as an empty date rather than as an exception: the card shows it, and a job whose
		// next run cannot be computed is a thing to SEE, not a thing to crash on.
		pvarRetValue = next.IsValid() ? ibValue(next) : ibValue(ibValueTypes::TYPE_DATE);
		return true;
	}
	case enPresentation:
		pvarRetValue = ibJobScheduleRules::Describe(m_schedule);
		return true;
	case enIsValid:
		pvarRetValue = m_schedule.IsValid();
		return true;
	}

	return false;
}

bool ibValueSchedule::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case enInterval:           m_schedule.m_intervalSeconds = varPropVal.GetInteger(); return true;
	case enStartTime:          m_schedule.m_startMinute = varPropVal.GetInteger(); return true;
	case enEndTime:            m_schedule.m_endMinute = varPropVal.GetInteger(); return true;
	case enStopAfter:          m_schedule.m_stopAfterMinute = varPropVal.GetInteger(); return true;
	case enDaysOfWeek:         m_schedule.m_daysOfWeek = static_cast<std::uint8_t>(varPropVal.GetInteger()); return true;
	case enDaysOfMonth:        m_schedule.m_daysOfMonth = static_cast<std::uint32_t>(varPropVal.GetInteger()); return true;
	case enDaysOfMonthFromEnd: m_schedule.m_daysOfMonthFromEnd = static_cast<std::uint32_t>(varPropVal.GetInteger()); return true;
	case enMonths:             m_schedule.m_months = static_cast<std::uint16_t>(varPropVal.GetInteger()); return true;
	case enWeekdayOrdinal:     m_schedule.m_weekdayOrdinal = static_cast<std::uint8_t>(varPropVal.GetInteger()); return true;
	case enEveryNWeeks:        m_schedule.m_everyNWeeks = static_cast<std::uint16_t>(varPropVal.GetInteger()); return true;
	case enEveryNMonths:       m_schedule.m_everyNMonths = static_cast<std::uint16_t>(varPropVal.GetInteger()); return true;
	case enPeriodAnchor:       m_schedule.m_periodAnchor = varPropVal.GetDateTime(); return true;
	case enActiveFrom:         m_schedule.m_activeFrom = varPropVal.GetDateTime(); return true;
	case enActiveTo:           m_schedule.m_activeTo = varPropVal.GetDateTime(); return true;
	}

	return false;
}

bool ibValueSchedule::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enInterval:           pvarPropVal = m_schedule.m_intervalSeconds; return true;
	case enStartTime:          pvarPropVal = m_schedule.m_startMinute; return true;
	case enEndTime:            pvarPropVal = m_schedule.m_endMinute; return true;
	case enStopAfter:          pvarPropVal = m_schedule.m_stopAfterMinute; return true;
	case enDaysOfWeek:         pvarPropVal = static_cast<int>(m_schedule.m_daysOfWeek); return true;
	case enDaysOfMonth:        pvarPropVal = static_cast<int>(m_schedule.m_daysOfMonth); return true;
	case enDaysOfMonthFromEnd: pvarPropVal = static_cast<int>(m_schedule.m_daysOfMonthFromEnd); return true;
	case enMonths:             pvarPropVal = static_cast<int>(m_schedule.m_months); return true;
	case enWeekdayOrdinal:     pvarPropVal = static_cast<int>(m_schedule.m_weekdayOrdinal); return true;
	case enEveryNWeeks:        pvarPropVal = static_cast<int>(m_schedule.m_everyNWeeks); return true;
	case enEveryNMonths:       pvarPropVal = static_cast<int>(m_schedule.m_everyNMonths); return true;
	// An unset date travels as the empty date, the same value an unfilled date requisite holds —
	// so "no anchor" and "no date typed" are one thing in script, not two.
	case enPeriodAnchor:       pvarPropVal = m_schedule.m_periodAnchor.IsValid() ? ibValue(m_schedule.m_periodAnchor) : ibValue(ibValueTypes::TYPE_DATE); return true;
	case enActiveFrom:         pvarPropVal = m_schedule.m_activeFrom.IsValid() ? ibValue(m_schedule.m_activeFrom) : ibValue(ibValueTypes::TYPE_DATE); return true;
	case enActiveTo:           pvarPropVal = m_schedule.m_activeTo.IsValid() ? ibValue(m_schedule.m_activeTo) : ibValue(ibValueTypes::TYPE_DATE); return true;
	}

	return false;
}

// CREATABLE, unlike the two below: a schedule is a value one composes ("every ten minutes, not
// before 02:00") before there is anything to attach it to.
VALUE_TYPE_REGISTER(ibValueSchedule, "JobSchedule", g_valueScheduleCLSID);

//**********************************************************************
//*                        Open me — ShowValue                         *
//**********************************************************************

#include "backend/backend_mainFrame.h"   // ibBackendDocFrame::ShowScheduleEditor — the frontend door
#include "backend/session/session.h"     // ibSession::CurrentFrame

void ibValueSchedule::ShowValue()
{
	// The value asks the CURRENT FRAME to open it — the same route a reference takes to its card
	// and a spreadsheet takes to its window. Which window that is, and that it is a wx dialog at
	// all, stays on the other side of this call.
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		ibBackendCoreException::Error(_("a schedule cannot be opened without an interactive context"));

	// Edited IN PLACE. Nothing is notified afterwards and nothing needs to be: whoever displays a
	// schedule displays its sentence, and the sentence is generated from these very fields — a
	// re-read is the whole update.
	frame->ShowScheduleEditor(m_schedule);
}

//////////////////////////////////////////////////////////////////////
//                        the collection                            //
//////////////////////////////////////////////////////////////////////

ibValuePredefinedJobs::ibValuePredefinedJobs(ibMetaData* metaData)
	: ibValueArray()
{
	if (metaData == nullptr)
		return;

	for (const auto object : metaData->GetAnyArrayObject(g_metaScheduledJobCLSID)) {
		if (object->IsDeleted())
			continue;
		ibValuePtr<ibValueJobRow> row(new ibValueJobRow(object->GetGuid(), object->GetName()));
		Add(row);
	}
}

//////////////////////////////////////////////////////////////////////
//                            the row                               //
//////////////////////////////////////////////////////////////////////

ibValuePredefinedJobs::ibValueJobRow::ibValueJobRow()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValuePredefinedJobs::ibValueJobRow::ibValueJobRow(const ibGuid& jobKey, const wxString& jobName)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_jobKey(jobKey), m_jobName(jobName)
{
	ReadSettings();
}

bool ibValuePredefinedJobs::ibValueJobRow::ReadSettings()
{
	if (!m_jobKey.isValid())
		return false;

	// The base row is the truth. A base that has never seen this job answers "not found" and the
	// defaults stand — the same values the manager seeds it with on the next registration.
	const ibJobManager::ibJobSettings stored = ibJobManager::ReadSharedSettings(m_jobKey);

	m_active = stored.m_active;
	m_lastRun = stored.m_lastRun;
	m_computer = stored.m_computer;
	if (stored.m_schedule.IsValid())
		m_schedule = stored.m_schedule;

	return stored.m_found;
}

void ibValueJobRow_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Name"));
	helper.AppendProp(wxT("Active"));
	helper.AppendProp(wxT("Schedule"));
	helper.AppendProp(wxT("LastRun"));
	helper.AppendProp(wxT("Computer"));
	helper.AppendProp(wxT("NextRun"));

	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	helper.AppendFunc(wxT("Execute"), wxT("Execute()"));
}

bool ibValuePredefinedJobs::ibValueJobRow::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRead:
		pvarRetValue = ReadSettings();
		return true;
	case enWrite:
	{
		if (!m_jobKey.isValid())
			return false;

		ibJobManager::ibJobSettings settings;
		settings.m_key = m_jobKey;
		settings.m_name = m_jobName;
		settings.m_active = m_active;
		settings.m_schedule = m_schedule;

		// BOTH halves, in this order: the row is what survives a restart, the live entry is what
		// the next tick reads. Writing only the row would leave a job running on a schedule nobody
		// can see any more; applying only in memory would lose the change on close.
		ibJobManager::WriteSharedSettings(settings);

		if (ibJobManager* const manager = ibApplicationData::GetJobManager())
			manager->ApplySettings(m_jobKey, m_active, m_schedule);

		pvarRetValue = true;
		return true;
	}
	case enExecute:
	{
		// RUN IT NOW — ignoring the calendar AND the switch. Switching a job off takes it off the
		// schedule; it does not take away the ability to fire it once and watch what happens,
		// which is usually the very next thing one does.
		//
		// The manager's own handle on an entry is its NAME, so the key is resolved through it
		// rather than assumed: a job not registered in this session simply says false.
		ibJobManager* const manager = ibApplicationData::GetJobManager();
		if (manager == nullptr) {
			pvarRetValue = false;
			return true;
		}
		const wxString registeredName = manager->FindNameByKey(m_jobKey);
		pvarRetValue = !registeredName.IsEmpty() && manager->RunNow(registeredName);
		return true;
	}
	}

	return false;
}

bool ibValuePredefinedJobs::ibValueJobRow::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case enActive:
		m_active = varPropVal.GetBoolean();
		return true;
	case enSchedule:
	{
		// Assigning a JobSchedule replaces the whole description — the value IS the schedule, so
		// there is no field-by-field copy to keep in step.
		ibValueSchedule* schedule = nullptr;
		if (!varPropVal.ConvertToValue(schedule) || schedule == nullptr)
			return false;
		m_schedule = schedule->GetSchedule();
		return true;
	}
	}

	// Name / LastRun / Computer / NextRun are FACTS, not settings: two belong to the past, one is
	// the caption, the fourth is derived. Refusing beats accepting a write the next Read undoes.
	return false;
}

bool ibValuePredefinedJobs::ibValueJobRow::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enName:
		pvarPropVal = m_jobName;
		return true;
	case enActive:
		pvarPropVal = m_active;
		return true;
	case enSchedule:
		// A COPY, deliberately: `job.Schedule.Interval = 600` alone would edit a temporary, so the
		// protocol is read it, change it, assign it back, then Write().
		pvarPropVal = new ibValueSchedule(m_schedule);
		return true;
	case enLastRun:
		pvarPropVal = m_lastRun.IsValid() ? ibValue(m_lastRun) : ibValue(ibValueTypes::TYPE_DATE);
		return true;
	case enComputer:
		pvarPropVal = m_computer;
		return true;
	case enNextRun:
	{
		// DERIVED, exactly as on a parameterized job's row: a pure function of the schedule and
		// the last run, computed when asked. Nothing stores it, so nothing can hold a stale copy
		// after a restart or a clock change.
		const wxDateTime countFrom = m_lastRun.IsValid()
			? m_lastRun + wxTimeSpan::Seconds(m_schedule.m_intervalSeconds > 0 ? m_schedule.m_intervalSeconds : 0)
			: wxDateTime::Now();

		const wxDateTime next = ibJobScheduleRules::NextAllowedAfter(m_schedule, countFrom);
		pvarPropVal = next.IsValid() ? ibValue(next) : ibValue(ibValueTypes::TYPE_DATE);
		return true;
	}
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// Both VENDED, never creatable: the collection exists because a configuration declares jobs, and a
// row exists because the collection produced it. `New JobSettings()` would refer to nothing — the
// way in is ScheduledJobs.Predefined.
SYSTEM_TYPE_REGISTER(ibValuePredefinedJobs, "PredefinedJobs", value_to_clsid("VL_PJOBS"));
SYSTEM_TYPE_REGISTER(ibValuePredefinedJobs::ibValueJobRow, "JobSettings", value_to_clsid("VL_JOBST"));
