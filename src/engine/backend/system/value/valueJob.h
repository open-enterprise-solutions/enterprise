#ifndef __VALUE_JOB_H__
#define __VALUE_JOB_H__

// THE SCRIPT VALUES OF THE SCHEDULED-JOB SUBSYSTEM — one file, because they are one subject:
//
//   ibValueSchedule          — WHEN a job is due, as a value. Both kinds carry one, and it is
//                              also the TYPE of a parameterized job's Schedule requisite.
//   ibValuePredefinedJobs    — every predefined job of this configuration, as an array …
//   …::ibValueJobRow         — … whose element is one job's live settings.
//
// The collection and its element are one type and its part, so the element is a NESTED class: it
// has no life outside the collection that produced it, and nesting says that in the type rather
// than in a comment two files away. The schedule sits beside them because it is what both of them
// are ABOUT — a job list that could not name a schedule would be a list of nothing.

#include "valueArray.h"
#include "backend/compiler/value.h"
#include "backend/job/jobSchedule.h"

class ibMetaData;

void ibValueSchedule_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
void ibValueJobRow_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

//////////////////////////////////////////////////////////////////////
//  JobSchedule — the engine's own schedule (ibJobScheduleDescription) as a SCRIPT value.
//
//  It exists because a parameterized scheduled job carries its schedule in a ROW, not in the
//  metadata: the row is the value, and a row's requisite has to be a type the language can hold,
//  read and assign. The metadata half (a designer property + its four-tab dialog) was already
//  there; this is the same description reaching the other three surfaces — script, the column
//  codec and the form.
//
//  It wraps the description rather than restating it. Every field is one property, the two
//  questions a schedule answers are the two methods (Allowed / NextRun), and the sentence a list
//  or a log shows is Presentation() — all forwarded to ibJobScheduleRules, so a schedule can never
//  mean one thing in a report and another in the manager.
//
//    job.Schedule.Interval = 600;             // every ten minutes
//    job.Schedule.StartTime = 2 * 60;         // ... but not before 02:00
//    job.Write();
//////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueSchedule : public ibValueStaticMembers<&ibValueSchedule_BindNames> {
	public:
private:

	// The fields of the description, one property each — in ITS declaration order, so the two
	// lists stay readable side by side. Times are MINUTES from midnight (02:30 is expressible)
	// and the day / month sets are the same bit masks the description holds: a mask is what the
	// dialog produces and what the rules read, so handing script a different shape would mean a
	// conversion nobody asked for.
	enum Prop {
		enInterval,
		enStartTime,
		enEndTime,
		enStopAfter,
		enDaysOfWeek,
		enDaysOfMonth,
		enDaysOfMonthFromEnd,
		enMonths,
		enWeekdayOrdinal,
		enEveryNWeeks,
		enEveryNMonths,
		enPeriodAnchor,
		enActiveFrom,
		enActiveTo,
	};

	// The rules, not a second copy of them.
	enum Func {
		enAllowed,        // does the CALENDAR allow this moment
		enNextRun,        // the first allowed moment at or after the given one
		enPresentation,   // the localised sentence ("Every 10 minutes, 10:00-15:00, Tue")
		enIsValid,        // is the description well formed (an editor's question)
	};

public:

	ibValueSchedule();
	explicit ibValueSchedule(const ibJobScheduleDescription& schedule);
	virtual ~ibValueSchedule() {}

	ibJobScheduleDescription& GetSchedule() { return m_schedule; }
	const ibJobScheduleDescription& GetSchedule() const { return m_schedule; }
	void SetSchedule(const ibJobScheduleDescription& schedule) { m_schedule = schedule; }

	// New JobSchedule() — a default schedule (hourly, no calendar restriction), which is exactly
	// what an untouched control produces. The one-argument form takes the interval in seconds,
	// the common shape ibJobScheduleDescription::EverySeconds already names.
	virtual bool Init() { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	// The sentence — the same one the manager's journal and the designer's property row show.
	virtual wxString GetString() const { return ibJobScheduleRules::Describe(m_schedule); }

	// A schedule is never "empty": it always answers when its job is due. Saying otherwise would
	// make an untouched schedule read as an absent one, and an absent schedule is a job that
	// never runs.
	virtual bool IsEmpty() const { return false; }

	// OPEN ME — the same verb a reference answers when somebody clicks it, and the same route: the
	// value asks the current frame, the frame opens whatever window it uses for this kind. Editing
	// happens IN PLACE, so whoever holds this value already has the new one when the call returns;
	// there is nothing to re-assign and nothing to re-create.
	//
	// No frame means no interactive context (daemon, codeRunner, a background job) — and that is
	// said out loud rather than silently ignored: a script that opens an editor where none can be
	// opened is a mistake, not a no-op.
	virtual void ShowValue() override;

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	operator ibJobScheduleDescription() const { return m_schedule; }

private:
	ibJobScheduleDescription m_schedule;
};

// The clsid the column layout gates its blob slot on and the type descriptor names — declared
// next to the value so the tier does not have to spell the string.
constexpr ibClassID g_valueScheduleCLSID = value_to_clsid("VL_SCHED");

//////////////////////////////////////////////////////////////////////
//  ScheduledJobs.Predefined — every predefined job of this configuration, as one walkable
//  collection, with the settings of ONE job as its element.
//
//  It is an ARRAY with parameters: the array is what a caller wants (walk them, count them, look
//  at each), and what it is parameterised BY is the metadata it was built from. Everything a plain
//  array can do it can do, because it IS one; what it adds is knowing how to fill itself.
//
//    For Each job In ScheduledJobs.Predefined Do
//        If job.Active And job.Schedule.Interval < 60 Then
//            job.Active = False;
//            job.Write();
//        EndIf;
//    EndDo;
//
//  The element is a VIEW on the base row, not a copy of the scheduler: Write() stores the values
//  in sys_job AND applies them to the registered job at once, so the tick and the card can never
//  hold different opinions about whether a job runs.
//
//  Why LIVE settings rather than the metaobject: the declaration answers what the developer wrote,
//  and the question here is what the base is doing — is this job on, when did it last run, when is
//  it due. Those live in the base (docs/scheduled-jobs.md § 8), because switching a misbehaving
//  job off must not mean opening the Designer against production.
//////////////////////////////////////////////////////////////////////

class BACKEND_API ibValuePredefinedJobs : public ibValueArray {
	public:

	// ONE predefined job's live settings — the element of the collection above.
	class BACKEND_API ibValueJobRow : public ibValueStaticMembers<&ibValueJobRow_BindNames> {
		public:
	private:

		enum Prop {
			enName,        // the job's name — what a person reads
			enActive,      // on the schedule? (running it by hand ignores this)
			enSchedule,    // a JobSchedule value — the same description the manager evaluates
			enLastRun,     // when it last ran, as every process on this base sees it
			enComputer,    // who ran it last
			enNextRun,     // DERIVED — computed from (schedule, last run) on read, never stored
		};

		enum Func {
			enRead,        // re-read the row (a peer may have changed it)
			enWrite,       // store it AND apply it to the registered job
			enExecute,     // run it now, whatever the schedule and the switch say
		};

	public:

		ibValueJobRow();
		// Keyed by the metaobject's GUID, never by its name: the guid is the identity that
		// survives a rename, an unload / reload and a copy onto another base, while a row keyed by
		// the old name is an orphan that still says "switched off" while the job runs.
		ibValueJobRow(const ibGuid& jobKey, const wxString& jobName);
		virtual ~ibValueJobRow() {}

		bool ReadSettings();

		virtual wxString GetString() const { return m_jobName; }
		virtual bool IsEmpty() const { return !m_jobKey.isValid(); }

		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	private:

		ibGuid                   m_jobKey;    // the metaobject GUID — the row is this job, whatever it is called
		wxString                 m_jobName;   // the display name
		bool                     m_active = true;
		ibJobScheduleDescription m_schedule;
		wxDateTime               m_lastRun;
		wxString                 m_computer;
	};

	ibValuePredefinedJobs() : ibValueArray() {}

	// THE PARAMETER — the metadata the collection is built from. Filling happens here rather than
	// in the caller, so "what counts as a predefined job" is answered once.
	explicit ibValuePredefinedJobs(ibMetaData* metaData);

	virtual wxString GetClassName() const { return wxT("PredefinedJobs"); }
};

#endif // !__VALUE_JOB_H__
