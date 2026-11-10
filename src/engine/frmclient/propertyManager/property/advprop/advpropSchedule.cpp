#include "advpropSchedule.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the schedule's node
#include "frmclient/mainFrame/objinspect/objinspect.h"                  // the object shown — the property is found on it again
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"                   // ibPGEditor_HyperLink — the click-only editor
#include "frmclient/win/dlgs/jobSchedule/jobScheduleSettings.h"

#include "core/description/jobScheduleDescription.h"

// -----------------------------------------------------------------------
// ibPGScheduleProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGScheduleProperty, ibPGProperty, HyperLink)

// register frontend property — ibPropertySchedule; the schedule as the server stores it (ibJobScheduleDescriptionMemory)
// is read when the dialog opens, so the cell carries only the row's words.
class ibPropertyScheduleLoader
{
public:
	ibPropertyScheduleLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySchedule"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGScheduleProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}g_scheduleLoader;

ibPGScheduleProperty::ibPGScheduleProperty(ibPropertyObject* property, const wxString& label,
	const wxString& name, const ibVariant& value) : ibPGProperty(label, name), m_ownerProperty(property) {

	ibPGProperty::SetFlagRecursively(ibPGFlags::ReadOnly, true);
	ibPGProperty::SetFlagRecursively(ibPGFlags::NoEditor, true);

	// THE CELL KEEPS THE VARIANT IT WAS GIVEN — the click stamps the NAME onto it rather than
	// replacing it (propertyEditor.cpp), so there is nothing to overwrite here.
	ibPGProperty::SetValue(value);
}

ibPGScheduleProperty::~ibPGScheduleProperty()
{
}

// A fixed label, not the schedule itself. Showing the sentence here was tempting — and wrong in
// practice: the row is rendered once and the dialog does not re-enter the grid, so after an edit
// the cell kept describing the OLD schedule. A stale description is worse than no description,
// because it is read as the value. The sentence lives in the dialog, where it is always current.
wxString ibPGScheduleProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	return _("Open");
}

bool ibPGScheduleProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGScheduleProperty::OnEvent(ibPropertyGrid* WXUNUSED(propgrid), wxWindow* WXUNUSED(wnd_primary),
	wxEvent& event)
{
	// THE CLICK (the hyperlink's editor, propertyEditor.cpp).
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		ibObjectInspector* const inspector = ibObjectInspector::GetObjectInspector();
		ibPropertyObject* const owner = m_ownerProperty;
		if (inspector == nullptr || owner == nullptr)
			return false;
		// Deferred: the click is still being dispatched, and a modal dialog opened from inside a
		// grid event runs the event loop over a property the grid may rebuild.
		wxTheApp->CallAfter(
			[inspector, owner, name = GetName()]() {
				// ⚠ FOUND AGAIN, BY ITS NAME, ON THE OWNER WHILE IT IS STILL THE ONE SHOWN (alive — the inspector holds
				// it) — before the dialog and after it: the row it stood for is the server's, and an answer meanwhile
				// replaces it.
				ibProperty* property = inspector->GetSelectedObject() == owner ? owner->GetProperty(name) : nullptr;
				if (property == nullptr)
					return;

				const ibVariant held = property->GetValue();
				ibJobScheduleDescription schedule;
				ibJobScheduleDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(held), schedule);
				ibDialogJobSchedule dlg(nullptr, schedule);
				if (dlg.ShowModal() != wxID_OK)
					return;

				if (dlg.GetSchedule() == schedule)
					return;   // nothing actually changed — do not mark the configuration modified

				// …written as an edit in the grid writes it (ibObjectInspector::ModifyProperty): the owner puts it before
				// the server, which marks the configuration modified.
				property = inspector->GetSelectedObject() == owner ? owner->GetProperty(name) : nullptr;
				ibDataValue node;
				if (property != nullptr && ibJobScheduleDescriptionMemory::WriteNode(node, dlg.GetSchedule()))
					ibPropertyGate::SetValue(owner, property,
						ibVariant(new ibVariantSerializable(node, held.GetString())));
			}
		);
		return true;
	}
	return false;
}

void ibPGScheduleProperty::RefreshChildren()
{
	ibPGProperty::Enable(true);
}
