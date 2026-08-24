#include "advpropSchedule.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"                   // wxPGEditor_HyperLink — the click-only editor
#include "frontend/win/dlgs/jobSchedule/jobScheduleSettings.h"

#include "backend/propertyManager/property/propertySchedule.h"
#include "backend/job/jobSchedule.h"

// -----------------------------------------------------------------------
// ibPGScheduleProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGScheduleProperty, wxPGProperty, HyperLink)

// register frontend property — the dynamic_cast in the registry IS the type match, so naming
// ibPropertySchedule in the lambda's parameter is the whole registration
// (docs/property-system.md § 4.1).
class ibPropertyScheduleLoader
{
public:
	ibPropertyScheduleLoader()
	{
		ibPropertyRegistry::Register([](ibPropertySchedule* prop) -> wxPGProperty* {
			return new ibPGScheduleProperty(prop, prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}g_scheduleLoader;

ibPGScheduleProperty::ibPGScheduleProperty(ibPropertySchedule* property, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_scheduleProperty(property) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	// THE CELL KEEPS THE VARIANT IT WAS GIVEN — the click stamps the NAME onto it rather than
	// replacing it (propertyEditor.cpp), so there is nothing to overwrite here.
	wxPGProperty::SetValue(value);
}

ibPGScheduleProperty::~ibPGScheduleProperty()
{
}

// A fixed label, not the schedule itself. Showing the sentence here was tempting — and wrong in
// practice: the row is rendered once and the dialog does not re-enter the grid, so after an edit
// the cell kept describing the OLD schedule. A stale description is worse than no description,
// because it is read as the value. The sentence lives in the dialog, where it is always current.
wxString ibPGScheduleProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return _("Open");
}

bool ibPGScheduleProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

void ibPGScheduleProperty::OnSetValue()
{
	// THE NAME IS THE CLICK — see the dynamic list's cell: the editor stamps the name onto whatever
	// the cell carries, so testing the bool would be testing the payload instead of the signal.
	if (wxT("hyperLink_clicked") == m_value.GetName()) {
		// ⭐ TAKEN, SO ENDED — see the hyperlink's cell, where a name that outlived its click cost two
		// crashes.
		m_value.SetName(wxEmptyString);

		ibPropertySchedule* property = m_scheduleProperty;
		if (property == nullptr)
			return;
		// Deferred: the click is still being dispatched, and a modal dialog opened from inside a
		// grid event runs the event loop over a property the grid may rebuild.
		wxTheApp->CallAfter(
			[property]() {
				ibDialogJobSchedule::ShowScheduleDialog(property);
			}
		);
	}
}

void ibPGScheduleProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
