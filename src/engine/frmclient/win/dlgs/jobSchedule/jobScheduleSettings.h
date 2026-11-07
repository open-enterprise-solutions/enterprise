#ifndef _FRMCLIENT_JOB_SCHEDULE_DLG_H__
#define _FRMCLIENT_JOB_SCHEDULE_DLG_H__

// The visual editor for ibJobScheduleDescription — a modal dialog with four tabs, the shape users
// already know from comparable systems: General / Daily / Weekly / Monthly.
//
// It edits a BUFFER, not the property: the description is copied in when the dialog opens and
// written back only on OK, so Cancel really cancels. Same transactional shape as the list-settings
// dialog it is modelled on (frmclient/win/dlgs/settings/list/listSettings.h).
//
// Every control here maps to exactly one field of the description, which is the whole reason the
// schedule was NOT designed as a cron string: an expression has to be parsed to be shown, and
// unparsed to be saved, and the two drift.

#include "deskgui/univ/dialog.h"
#include "deskgui/univ/ctrl/checkListBox.h"
#include "deskgui/univ/ctrl/checkBox.h"
#include "deskgui/univ/ctrl/spinCtrl.h"
#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/ctrl/staticText.h"
#include <wx/datectrl.h>
#include <wx/timectrl.h>

#include "frmclient/frmclient.h"   // FRMCLIENT_API
#include "core/job/jobSchedule.h"

// The old desktop's, asked by the server (ibProtocol::ibRequest::ibKind::Schedule): the schedule
// comes in the question, the edited one is the answer.
class FRMCLIENT_API ibDialogJobSchedule : public ibDialog {
public:

	ibDialogJobSchedule(wxWindow* parent, const ibJobScheduleDescription& schedule);

	// The edited value — read after ShowModal() returns wxID_OK.
	const ibJobScheduleDescription& GetSchedule() const { return m_schedule; }

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
	static int  MinutesFrom(wxTimePickerCtrl* ctrl, const ibCheckBox* use);
	static void MinutesTo(wxTimePickerCtrl* ctrl, ibCheckBox* use, int minutes);
	static void MaskTo(ibCheckListBox* list, std::uint32_t mask);
	static std::uint32_t MaskFrom(const ibCheckListBox* list);

	ibJobScheduleDescription m_schedule;   // the buffer

	// General
	ibSpinCtrl*        m_intervalValue   = nullptr;
	ibChoice*          m_intervalUnit    = nullptr;   // seconds / minutes / hours / days
	ibCheckBox*        m_activeFromUse   = nullptr;
	wxDatePickerCtrl*  m_activeFrom      = nullptr;
	ibCheckBox*        m_activeToUse     = nullptr;
	wxDatePickerCtrl*  m_activeTo        = nullptr;

	// Daily
	ibCheckBox*        m_windowUse       = nullptr;
	wxTimePickerCtrl*  m_windowStart     = nullptr;
	wxTimePickerCtrl*  m_windowEnd       = nullptr;
	ibCheckBox*        m_stopAfterUse    = nullptr;
	wxTimePickerCtrl*  m_stopAfter       = nullptr;

	// Weekly
	ibCheckListBox*    m_weekDays        = nullptr;
	ibSpinCtrl*        m_everyNWeeks     = nullptr;
	ibChoice*          m_weekdayOrdinal  = nullptr;   // none / first … fifth / last

	// Monthly
	ibCheckListBox*    m_monthDays       = nullptr;
	ibCheckListBox*    m_monthDaysEnd    = nullptr;
	ibCheckListBox*    m_months          = nullptr;
	ibSpinCtrl*        m_everyNMonths    = nullptr;

	ibStaticText*      m_description     = nullptr;
};

#endif // _FRMCLIENT_JOB_SCHEDULE_DLG_H__
