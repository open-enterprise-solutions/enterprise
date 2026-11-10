#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGHyperLinkProperty, ibPGProperty, HyperLink)

ibPGHyperLinkProperty::ibPGHyperLinkProperty(ibMetaID id, const ibMetaData& metaData, const wxString& label,
	const wxString& name, const ibVariant& value) : ibPGProperty(label, name), m_metaId(id), m_metaData(metaData) {

	ibPGProperty::SetFlagRecursively(ibPGFlags::ReadOnly, true);
	//ibPGProperty::SetFlagRecursively(ibPGFlags::Hidden, true);
	ibPGProperty::SetFlagRecursively(ibPGFlags::NoEditor, true);

	// The value is the property's, as it was given: the click is not in it — it comes to OnEvent below, as an event
	// (propertyEditor.cpp). 🛑 A click held in the value outlives it: when it was a name on wx's variant, a cell born
	// carrying it read every SetValue the grid makes (a rebuild, a refresh, a re-selection) as a click, and the deferred
	// OpenObjectForms fired against a document list that had moved on (two dumps, 2026-08-24).
	ibPGProperty::SetValue(value);
}

ibPGHyperLinkProperty::~ibPGHyperLinkProperty()
{
}

wxString ibPGHyperLinkProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	return _("Open");
}

bool ibPGHyperLinkProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	return false;
}

#include "frmclient/docView/docView.h"   // docManager — the door that opens a metaobject

bool ibPGHyperLinkProperty::OnEvent(ibPropertyGrid* WXUNUSED(propgrid), wxWindow* WXUNUSED(wnd_primary),
	wxEvent& event)
{
	// THE CLICK (the hyperlink's editor, propertyEditor.cpp) — the cell's own value is `false` from its constructor,
	// the click is not in it.
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		// THE LINK CARRIES ITS OBJECT — the one the server named, in the configuration its owner was opened from; the
		// server opens it. Taken by value: the cell goes with the next selection.
		if (m_metaId != 0) {

			// ⚠ DEFERRED, because the click is still being dispatched: opening a document from
			// inside the grid's own event tears down the cell that is handling it.
			wxTheApp->CallAfter(
				[id = m_metaId, metaData = m_metaData]() {

					// ⭐⭐ JUST OPEN IT. Whose it is — the document holding that configuration, or
					// nobody's when it is the one the main window shows — is worked out INSIDE
					// OpenForm, from the object itself (Max, 2026-09-01: *"it is a very complicated
					// mechanism otherwise — you would have to track everywhere on whose behalf it
					// was opened"*).
					docManager->OpenForm(id, metaData);
				}
			);
		}
		return true;
	}
	return false;
}

void ibPGHyperLinkProperty::RefreshChildren()
{
	ibPGProperty::Enable(true);
}
