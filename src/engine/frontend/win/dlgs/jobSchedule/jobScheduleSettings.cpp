////////////////////////////////////////////////////////////////////////////
//	Description : the schedule editor — four tabs over ibJobScheduleDescription
////////////////////////////////////////////////////////////////////////////

#include "jobScheduleSettings.h"

#include "backend/propertyManager/property/propertySchedule.h"
#include "backend/metaData.h"                    // ibMetaData::Modify — the owner hands it over directly

#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

namespace {

// Interval units offered in the General tab. Seconds is the storage unit; the rest are multipliers,
// picked so the common cadences read as one number instead of 86400.
const int s_unitSeconds[] = { 1, 60, 3600, 86400 };

// Split a stored interval into the LARGEST unit that divides it exactly — "every 2 hours" reads
// better than "every 7200 seconds", and a value that does not divide cleanly stays in seconds
// rather than being rounded into a lie.
void SplitInterval(int seconds, int& value, int& unitIndex)
{
	for (int idx = 3; idx >= 0; --idx) {
		if (seconds >= s_unitSeconds[idx] && (seconds % s_unitSeconds[idx]) == 0) {
			value = seconds / s_unitSeconds[idx];
			unitIndex = idx;
			return;
		}
	}
	value = seconds > 0 ? seconds : 1;
	unitIndex = 0;
}

} // namespace

ibDialogJobSchedule::ibDialogJobSchedule(wxWindow* parent, const ibJobScheduleDescription& schedule)
	: wxDialog(parent, wxID_ANY, _("Schedule"), wxDefaultPosition, wxDefaultSize,
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN),
	  m_schedule(schedule)
{
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	notebook->AddPage(BuildGeneralPage(notebook), _("General"), true);
	notebook->AddPage(BuildDailyPage(notebook),   _("Daily"));
	notebook->AddPage(BuildWeeklyPage(notebook),  _("Weekly"));
	notebook->AddPage(BuildMonthlyPage(notebook), _("Monthly"));

	topSizer->Add(notebook, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(6)));

	// The sentence — what the manager will actually understand, shown while it is being built.
	m_description = new wxStaticText(this, wxID_ANY, wxEmptyString);
	topSizer->Add(m_description, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	topSizer->Add(buttons, wxSizerFlags(0).Right().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	SetSizer(topSizer);
	topSizer->SetSizeHints(this);
	SetSize(FromDIP(wxSize(440, 420)));
	Centre(wxBOTH);

	LoadFromSchedule();
	RefreshDescription();

	Bind(wxEVT_BUTTON, &ibDialogJobSchedule::OnOk, this, wxID_OK);
}

//***********************************************************************
//*                              the pages                              *
//***********************************************************************

wxWindow* ibDialogJobSchedule::BuildGeneralPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* intervalRow = new wxBoxSizer(wxHORIZONTAL);
	intervalRow->Add(new wxStaticText(page, wxID_ANY, _("Repeat every:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));

	m_intervalValue = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
	                                 wxSP_ARROW_KEYS, 1, 100000, 1);
	intervalRow->Add(m_intervalValue, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));

	wxArrayString units;
	units.Add(_("seconds")); units.Add(_("minutes")); units.Add(_("hours")); units.Add(_("days"));
	m_intervalUnit = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, units);
	intervalRow->Add(m_intervalUnit, wxSizerFlags(0).CenterVertical());

	sizer->Add(intervalRow, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(8)));

	// Validity range — the job does not run before / after these. Unchecked = unbounded, which is
	// what an untouched control has to mean; a default of "today" would silently bound the job.
	wxStaticBoxSizer* rangeBox = new wxStaticBoxSizer(wxVERTICAL, page, _("Validity"));

	wxBoxSizer* fromRow = new wxBoxSizer(wxHORIZONTAL);
	m_activeFromUse = new wxCheckBox(rangeBox->GetStaticBox(), wxID_ANY, _("Start date:"));
	m_activeFrom = new wxDatePickerCtrl(rangeBox->GetStaticBox(), wxID_ANY);
	fromRow->Add(m_activeFromUse, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	fromRow->Add(m_activeFrom, wxSizerFlags(0).CenterVertical());
	rangeBox->Add(fromRow, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(4)));

	wxBoxSizer* toRow = new wxBoxSizer(wxHORIZONTAL);
	m_activeToUse = new wxCheckBox(rangeBox->GetStaticBox(), wxID_ANY, _("End date:"));
	m_activeTo = new wxDatePickerCtrl(rangeBox->GetStaticBox(), wxID_ANY);
	toRow->Add(m_activeToUse, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	toRow->Add(m_activeTo, wxSizerFlags(0).CenterVertical());
	rangeBox->Add(toRow, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(4)));

	sizer->Add(rangeBox, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(8)));

	page->SetSizer(sizer);

	m_intervalValue->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { RefreshDescription(); });
	m_intervalUnit->Bind(wxEVT_CHOICE, &ibDialogJobSchedule::OnAnyChange, this);
	m_activeFromUse->Bind(wxEVT_CHECKBOX, &ibDialogJobSchedule::OnAnyChange, this);
	m_activeToUse->Bind(wxEVT_CHECKBOX, &ibDialogJobSchedule::OnAnyChange, this);

	return page;
}

wxWindow* ibDialogJobSchedule::BuildDailyPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// The time-of-day window. It WRAPS midnight when the start is later than the end (22:00–05:00
	// is a night window); read as a plain range it would match no minute at all.
	wxStaticBoxSizer* windowBox = new wxStaticBoxSizer(wxVERTICAL, page, _("Time window"));

	m_windowUse = new wxCheckBox(windowBox->GetStaticBox(), wxID_ANY, _("Run only inside a time window"));
	windowBox->Add(m_windowUse, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(4)));

	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(new wxStaticText(windowBox->GetStaticBox(), wxID_ANY, _("From:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(4)));
	m_windowStart = new wxTimePickerCtrl(windowBox->GetStaticBox(), wxID_ANY);
	row->Add(m_windowStart, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(10)));
	row->Add(new wxStaticText(windowBox->GetStaticBox(), wxID_ANY, _("to:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(4)));
	m_windowEnd = new wxTimePickerCtrl(windowBox->GetStaticBox(), wxID_ANY);
	row->Add(m_windowEnd, wxSizerFlags(0).CenterVertical());
	windowBox->Add(row, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(4)));

	sizer->Add(windowBox, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(8)));

	// Stop-after gates the START only: cutting a pass off mid-flight belongs to whoever knows what
	// half-done means for that work.
	wxStaticBoxSizer* stopBox = new wxStaticBoxSizer(wxVERTICAL, page, _("Do not start after"));
	wxBoxSizer* stopRow = new wxBoxSizer(wxHORIZONTAL);
	m_stopAfterUse = new wxCheckBox(stopBox->GetStaticBox(), wxID_ANY, _("Time:"));
	m_stopAfter = new wxTimePickerCtrl(stopBox->GetStaticBox(), wxID_ANY);
	stopRow->Add(m_stopAfterUse, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	stopRow->Add(m_stopAfter, wxSizerFlags(0).CenterVertical());
	stopBox->Add(stopRow, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(4)));

	sizer->Add(stopBox, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(8)));
	page->SetSizer(sizer);

	m_windowUse->Bind(wxEVT_CHECKBOX, &ibDialogJobSchedule::OnAnyChange, this);
	m_stopAfterUse->Bind(wxEVT_CHECKBOX, &ibDialogJobSchedule::OnAnyChange, this);

	return page;
}

wxWindow* ibDialogJobSchedule::BuildWeeklyPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxArrayString days;
	days.Add(_("Monday")); days.Add(_("Tuesday")); days.Add(_("Wednesday")); days.Add(_("Thursday"));
	days.Add(_("Friday")); days.Add(_("Saturday")); days.Add(_("Sunday"));
	m_weekDays = new wxCheckListBox(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, days);
	sizer->Add(new wxStaticText(page, wxID_ANY, _("Days of week (none checked = every day):")),
	           wxSizerFlags(0).Border(wxLEFT | wxTOP, FromDIP(8)));
	sizer->Add(m_weekDays, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));

	wxBoxSizer* everyRow = new wxBoxSizer(wxHORIZONTAL);
	everyRow->Add(new wxStaticText(page, wxID_ANY, _("Every N weeks:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	m_everyNWeeks = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 52, 1);
	everyRow->Add(m_everyNWeeks, wxSizerFlags(0).CenterVertical());
	sizer->Add(everyRow, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	// The Nth weekday of the month — it counts occurrences of the weekday checked above, so it is
	// only meaningful when exactly one day is checked.
	wxBoxSizer* ordinalRow = new wxBoxSizer(wxHORIZONTAL);
	ordinalRow->Add(new wxStaticText(page, wxID_ANY, _("Occurrence in month:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	wxArrayString ordinals;
	ordinals.Add(_("any")); ordinals.Add(_("first")); ordinals.Add(_("second")); ordinals.Add(_("third"));
	ordinals.Add(_("fourth")); ordinals.Add(_("fifth")); ordinals.Add(_("last"));
	m_weekdayOrdinal = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, ordinals);
	ordinalRow->Add(m_weekdayOrdinal, wxSizerFlags(0).CenterVertical());
	sizer->Add(ordinalRow, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	page->SetSizer(sizer);

	m_weekDays->Bind(wxEVT_CHECKLISTBOX, &ibDialogJobSchedule::OnAnyChange, this);
	m_weekdayOrdinal->Bind(wxEVT_CHOICE, &ibDialogJobSchedule::OnAnyChange, this);

	return page;
}

wxWindow* ibDialogJobSchedule::BuildMonthlyPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* listsRow = new wxBoxSizer(wxHORIZONTAL);

	wxArrayString days;
	for (int day = 1; day <= 31; ++day)
		days.Add(wxString::Format(wxT("%i"), day));
	wxBoxSizer* daysCol = new wxBoxSizer(wxVERTICAL);
	daysCol->Add(new wxStaticText(page, wxID_ANY, _("Days of month:")), wxSizerFlags(0));
	m_monthDays = new wxCheckListBox(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, days);
	daysCol->Add(m_monthDays, wxSizerFlags(1).Expand());
	listsRow->Add(daysCol, wxSizerFlags(1).Expand().Border(wxRIGHT, FromDIP(6)));

	// Counted from the END — a separate mask rather than a direction flag, because "the 1st and the
	// last" is an ordinary wish that one field with a direction cannot say.
	wxArrayString fromEnd;
	fromEnd.Add(_("last day"));
	for (int back = 1; back <= 30; ++back)
		fromEnd.Add(wxString::Format(_("%i before last"), back));
	wxBoxSizer* endCol = new wxBoxSizer(wxVERTICAL);
	endCol->Add(new wxStaticText(page, wxID_ANY, _("From the end:")), wxSizerFlags(0));
	m_monthDaysEnd = new wxCheckListBox(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, fromEnd);
	endCol->Add(m_monthDaysEnd, wxSizerFlags(1).Expand());
	listsRow->Add(endCol, wxSizerFlags(1).Expand().Border(wxRIGHT, FromDIP(6)));

	wxArrayString months;
	months.Add(_("January")); months.Add(_("February")); months.Add(_("March")); months.Add(_("April"));
	months.Add(_("May")); months.Add(_("June")); months.Add(_("July")); months.Add(_("August"));
	months.Add(_("September")); months.Add(_("October")); months.Add(_("November")); months.Add(_("December"));
	wxBoxSizer* monthsCol = new wxBoxSizer(wxVERTICAL);
	monthsCol->Add(new wxStaticText(page, wxID_ANY, _("Months:")), wxSizerFlags(0));
	m_months = new wxCheckListBox(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, months);
	monthsCol->Add(m_months, wxSizerFlags(1).Expand());
	listsRow->Add(monthsCol, wxSizerFlags(1).Expand());

	sizer->Add(listsRow, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));

	wxBoxSizer* everyRow = new wxBoxSizer(wxHORIZONTAL);
	everyRow->Add(new wxStaticText(page, wxID_ANY, _("Every N months:")), wxSizerFlags(0).CenterVertical().Border(wxRIGHT, FromDIP(6)));
	m_everyNMonths = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 12, 1);
	everyRow->Add(m_everyNMonths, wxSizerFlags(0).CenterVertical());
	sizer->Add(everyRow, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	page->SetSizer(sizer);

	m_monthDays->Bind(wxEVT_CHECKLISTBOX, &ibDialogJobSchedule::OnAnyChange, this);
	m_monthDaysEnd->Bind(wxEVT_CHECKLISTBOX, &ibDialogJobSchedule::OnAnyChange, this);
	m_months->Bind(wxEVT_CHECKLISTBOX, &ibDialogJobSchedule::OnAnyChange, this);

	return page;
}

//***********************************************************************
//*                        buffer <-> controls                          *
//***********************************************************************

int ibDialogJobSchedule::MinutesFrom(wxTimePickerCtrl* ctrl, const wxCheckBox* use)
{
	if (ctrl == nullptr || (use != nullptr && !use->GetValue()))
		return -1;
	int hour = 0, minute = 0, second = 0;
	if (!ctrl->GetTime(&hour, &minute, &second))
		return -1;
	return hour * 60 + minute;
}

void ibDialogJobSchedule::MinutesTo(wxTimePickerCtrl* ctrl, wxCheckBox* use, int minutes)
{
	const bool declared = minutes >= 0;
	if (use != nullptr)
		use->SetValue(declared);
	if (ctrl != nullptr)
		ctrl->SetTime(declared ? minutes / 60 : 0, declared ? minutes % 60 : 0, 0);
}

void ibDialogJobSchedule::MaskTo(wxCheckListBox* list, std::uint32_t mask)
{
	if (list == nullptr)
		return;
	for (unsigned int idx = 0; idx < list->GetCount(); ++idx)
		list->Check(idx, (mask & (1u << idx)) != 0);
}

std::uint32_t ibDialogJobSchedule::MaskFrom(const wxCheckListBox* list)
{
	std::uint32_t mask = 0;
	if (list == nullptr)
		return mask;
	for (unsigned int idx = 0; idx < list->GetCount(); ++idx)
		if (list->IsChecked(idx))
			mask |= (1u << idx);
	return mask;
}

void ibDialogJobSchedule::LoadFromSchedule()
{
	int value = 1, unitIndex = 0;
	SplitInterval(m_schedule.m_intervalSeconds, value, unitIndex);
	m_intervalValue->SetValue(value);
	m_intervalUnit->SetSelection(unitIndex);

	m_activeFromUse->SetValue(m_schedule.m_activeFrom.IsValid());
	if (m_schedule.m_activeFrom.IsValid())
		m_activeFrom->SetValue(m_schedule.m_activeFrom);
	m_activeToUse->SetValue(m_schedule.m_activeTo.IsValid());
	if (m_schedule.m_activeTo.IsValid())
		m_activeTo->SetValue(m_schedule.m_activeTo);

	const bool hasWindow = m_schedule.m_startMinute >= 0 && m_schedule.m_endMinute >= 0;
	m_windowUse->SetValue(hasWindow);
	MinutesTo(m_windowStart, nullptr, hasWindow ? m_schedule.m_startMinute : 0);
	MinutesTo(m_windowEnd,   nullptr, hasWindow ? m_schedule.m_endMinute   : 0);
	MinutesTo(m_stopAfter, m_stopAfterUse, m_schedule.m_stopAfterMinute);

	// An untouched day mask means EVERY day (ibJobWeekDay_Any), and the list shows that as nothing
	// checked — checking all seven would say the same thing while looking like a restriction.
	const bool everyDay = m_schedule.m_daysOfWeek == 0 || m_schedule.m_daysOfWeek == ibJobWeekDay_Any;
	MaskTo(m_weekDays, everyDay ? 0u : m_schedule.m_daysOfWeek);

	m_everyNWeeks->SetValue(m_schedule.m_everyNWeeks > 0 ? m_schedule.m_everyNWeeks : 1);
	m_everyNMonths->SetValue(m_schedule.m_everyNMonths > 0 ? m_schedule.m_everyNMonths : 1);

	m_weekdayOrdinal->SetSelection(
		m_schedule.m_weekdayOrdinal == ibJobOrdinal_Last ? 6 :
		(m_schedule.m_weekdayOrdinal >= 1 && m_schedule.m_weekdayOrdinal <= 5 ? m_schedule.m_weekdayOrdinal : 0));

	MaskTo(m_monthDays,    m_schedule.m_daysOfMonth);
	MaskTo(m_monthDaysEnd, m_schedule.m_daysOfMonthFromEnd);
	MaskTo(m_months,       m_schedule.m_months);
}

void ibDialogJobSchedule::ApplyToSchedule()
{
	const int unitIndex = m_intervalUnit->GetSelection() >= 0 ? m_intervalUnit->GetSelection() : 0;
	m_schedule.m_intervalSeconds = m_intervalValue->GetValue() * s_unitSeconds[unitIndex];

	m_schedule.m_activeFrom = m_activeFromUse->GetValue() ? m_activeFrom->GetValue() : wxDateTime();
	m_schedule.m_activeTo   = m_activeToUse->GetValue()   ? m_activeTo->GetValue()   : wxDateTime();

	if (m_windowUse->GetValue()) {
		m_schedule.m_startMinute = MinutesFrom(m_windowStart, nullptr);
		m_schedule.m_endMinute   = MinutesFrom(m_windowEnd,   nullptr);
	}
	else {
		m_schedule.m_startMinute = -1;
		m_schedule.m_endMinute   = -1;
	}
	m_schedule.m_stopAfterMinute = MinutesFrom(m_stopAfter, m_stopAfterUse);

	const std::uint32_t weekMask = MaskFrom(m_weekDays);
	m_schedule.m_daysOfWeek = weekMask == 0
		? static_cast<std::uint8_t>(ibJobWeekDay_Any)
		: static_cast<std::uint8_t>(weekMask & ibJobWeekDay_Any);

	m_schedule.m_daysOfMonth        = MaskFrom(m_monthDays);
	m_schedule.m_daysOfMonthFromEnd = MaskFrom(m_monthDaysEnd);
	m_schedule.m_months             = static_cast<std::uint16_t>(MaskFrom(m_months));

	const int ordinal = m_weekdayOrdinal->GetSelection();
	m_schedule.m_weekdayOrdinal = ordinal == 6 ? static_cast<std::uint8_t>(ibJobOrdinal_Last)
	                                           : static_cast<std::uint8_t>(ordinal > 0 ? ordinal : 0);

	// "Every 1" is the same as "every one", so it is stored as unused rather than as a period of
	// one — otherwise the description would name a restriction that restricts nothing.
	const int weeks  = m_everyNWeeks->GetValue();
	const int months = m_everyNMonths->GetValue();
	m_schedule.m_everyNWeeks  = static_cast<std::uint16_t>(weeks  > 1 ? weeks  : 0);
	m_schedule.m_everyNMonths = static_cast<std::uint16_t>(months > 1 ? months : 0);
}

void ibDialogJobSchedule::RefreshDescription()
{
	ApplyToSchedule();
	if (m_description == nullptr)
		return;

	// Sub-minute intervals are honoured since NextAllowedAfter returns an already-allowed moment
	// untouched; the minute grain only applies to the CALENDAR fields (windows, weekdays, months),
	// which cannot name anything finer.
	m_description->SetLabel(ibJobScheduleRules::Describe(m_schedule));
	Layout();
}

void ibDialogJobSchedule::OnAnyChange(wxCommandEvent& event)
{
	RefreshDescription();
	event.Skip();
}

void ibDialogJobSchedule::OnOk(wxCommandEvent& event)
{
	ApplyToSchedule();

	// Refuse what can never match, HERE, in front of the person who typed it — rather than leaving
	// it to be debugged later as a job that simply never runs.
	if (!m_schedule.IsValid()) {
		wxMessageBox(_("This schedule can never run: check the interval and the time window."),
		             _("Schedule"), wxOK | wxICON_WARNING, this);
		return;
	}

	event.Skip();   // let the dialog close with wxID_OK
}

//***********************************************************************
//*                            the entry                                *
//***********************************************************************

// static
bool ibDialogJobSchedule::ShowScheduleDialog(ibPropertySchedule* property)
{
	if (property == nullptr)
		return false;

	ibDialogJobSchedule dlg(nullptr, property->GetValueAsSchedule());
	if (dlg.ShowModal() != wxID_OK)
		return false;

	if (dlg.GetSchedule() == property->GetValueAsSchedule())
		return false;   // nothing actually changed — do not mark the configuration modified

	property->SetValue(dlg.GetSchedule());

	if (ibPropertyObject* owner = property->GetPropertyObject()) {
		// Raise the change from the property's REAL owner, so a holder reacts first and the signal
		// bubbles along the attach chain (docs/property-system.md § 8.3).
		owner->OnChildChanged();

		// MARK THE CONFIGURATION DIRTY. An ordinary grid edit reaches this through the inspector's
		// one write path; a dialog does not, so the metadata would stay "unmodified" and Save would
		// be greyed out over a schedule the user just changed — the edit survives until the
		// configuration is closed, and then quietly does not.
		//
		// Straight off the owner: a property object HAS its metadata, and the mutable overload is
		// the one ibValueMetaObject implements (propertyObject.h) — no cast to reach it.
		if (ibMetaData* metaData = owner->GetMetaData())
			metaData->Modify(true);
	}

	return true;
}
