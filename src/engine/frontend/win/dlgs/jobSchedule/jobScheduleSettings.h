#ifndef __JOB_SCHEDULE_DLG_H__
#define __JOB_SCHEDULE_DLG_H__

// The visual editor for ibJobScheduleDescription — a modal dialog with four tabs, the shape users
// already know from comparable systems: General / Daily / Weekly / Monthly.
//
// It edits a BUFFER, not the property: the description is copied in when the dialog opens and
// written back only on OK, so Cancel really cancels. Same transactional shape as the list-settings
// dialog it is modelled on (frontend/win/dlgs/settings/list/listSettings.h).
//
// Every control here maps to exactly one field of the description, which is the whole reason the
// schedule was NOT designed as a cron string: an expression has to be parsed to be shown, and
// unparsed to be saved, and the two drift.

#include <wx/dialog.h>
#include <wx/checklst.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/datectrl.h>
#include <wx/timectrl.h>

#include "backend/job/jobSchedule.h"

class BACKEND_API ibPropertySchedule;

class ibDialogJobSchedule : public wxDialog {
public:

	ibDialogJobSchedule(wxWindow* parent, const ibJobScheduleDescription& schedule);

	// The edited value — read after ShowModal() returns wxID_OK.
	const ibJobScheduleDescription& GetSchedule() const { return m_schedule; }

	// The designer entry: open the editor against a schedule property and write the result back on
	// OK. Returns true when the value changed. Mirrors ibDialogListSettings::ShowListSettingsDialog.
	static bool ShowScheduleDialog(ibPropertySchedule* property);

private:

	wxWindow* BuildGeneralPage(wxWindow* parent);
	wxWindow* BuildDailyPage(wxWindow* parent);
	wxWindow* BuildWeeklyPage(wxWindow* parent);
	wxWindow* BuildMonthlyPage(wxWindow* parent);

	void LoadFromSchedule();
	void ApplyToSchedule();

	// The sentence at the bottom — built by the RULES, so the dialog and a log line cannot disagree
	// about what the user just described. Refreshed on every edit.
	void RefreshDescription();
	void OnAnyChange(wxCommandEvent&);
	void OnOk(wxCommandEvent&);

	// Helpers for the two representations that differ between UI and storage: a time is minutes
	// from midnight (-1 = unset), and a mask is a check-list.
	static int  MinutesFrom(wxTimePickerCtrl* ctrl, const wxCheckBox* use);
	static void MinutesTo(wxTimePickerCtrl* ctrl, wxCheckBox* use, int minutes);
	static void MaskTo(wxCheckListBox* list, std::uint32_t mask);
	static std::uint32_t MaskFrom(const wxCheckListBox* list);

	ibJobScheduleDescription m_schedule;   // the buffer

	// General
	wxSpinCtrl*        m_intervalValue   = nullptr;
	wxChoice*          m_intervalUnit    = nullptr;   // seconds / minutes / hours / days
	wxCheckBox*        m_activeFromUse   = nullptr;
	wxDatePickerCtrl*  m_activeFrom      = nullptr;
	wxCheckBox*        m_activeToUse     = nullptr;
	wxDatePickerCtrl*  m_activeTo        = nullptr;

	// Daily
	wxCheckBox*        m_windowUse       = nullptr;
	wxTimePickerCtrl*  m_windowStart     = nullptr;
	wxTimePickerCtrl*  m_windowEnd       = nullptr;
	wxCheckBox*        m_stopAfterUse    = nullptr;
	wxTimePickerCtrl*  m_stopAfter       = nullptr;

	// Weekly
	wxCheckListBox*    m_weekDays        = nullptr;
	wxSpinCtrl*        m_everyNWeeks     = nullptr;
	wxChoice*          m_weekdayOrdinal  = nullptr;   // none / first … fifth / last

	// Monthly
	wxCheckListBox*    m_monthDays       = nullptr;
	wxCheckListBox*    m_monthDaysEnd    = nullptr;
	wxCheckListBox*    m_months          = nullptr;
	wxSpinCtrl*        m_everyNMonths    = nullptr;

	wxStaticText*      m_description     = nullptr;
};

#endif // __JOB_SCHEDULE_DLG_H__
