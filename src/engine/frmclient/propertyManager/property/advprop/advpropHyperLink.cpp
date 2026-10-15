#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGHyperLinkProperty, wxPGProperty, HyperLink)

#include "frmclient/backend/metaData.h"   // the owner's configuration — copied into the command

ibPGHyperLinkProperty::ibPGHyperLinkProperty(ibPropertyObject* ownerProperty, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_ownerProperty(ownerProperty) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	//wxPGProperty::SetFlagRecursively(wxPGFlags::Hidden, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	// 🛑 NO NAME HERE. This used to be born carrying `hyperLink_clicked`, which is the signal for
	// "somebody just clicked" — so the cell said it about ITSELF, for its whole life. Every SetValue
	// the grid makes (a rebuild, a refresh, a re-selection) then read as a click and queued another
	// deferred OpenObjectForm; those fired against a document list that had moved on, and the
	// designer went down inside ibDocManager::GetDocumentsVector (two dumps, 2026-08-24).
	//
	// The value is what it always was — false. The name is put on by the EDITOR at the click
	// (propertyEditor.cpp) and taken off by OnSetValue below, which is what makes it an event.
	wxPGProperty::SetValue(wxVariant(false));
}

ibPGHyperLinkProperty::~ibPGHyperLinkProperty()
{
}

wxString ibPGHyperLinkProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return _("Open");
}

bool ibPGHyperLinkProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // GetObjectId — the module the row names
#include "frmclient/docView/docView.h"   // docManager — the door that opens a metaobject

void ibPGHyperLinkProperty::OnSetValue()
{
	// THE NAME IS THE CLICK. The editor stamps the name onto whatever the cell carries and does not
	// set a bool any more (propertyEditor.cpp), so a `GetBool()` here would never be true again —
	// this cell's own value is `false` from its constructor.
	if (wxT("hyperLink_clicked") == m_value.GetName()) {
		// ⭐ TAKEN, SO ENDED — the same rule the composer's and the list's cells follow. A name left
		// standing turns one click into every later refresh.
		m_value.SetName(wxEmptyString);

		// THE LINK CARRIES ITS COMMAND — open the module its row names (Object), in the configuration its owner was
		// opened from; the server opens it. Taken now, by value: the owner is the inspector's, and goes with the next
		// selection.
		const ibPropertyObject* const owner = m_ownerProperty;
		const ibPropertySerializable* const property = owner != nullptr
			? dynamic_cast<const ibPropertySerializable*>(owner->GetProperty(GetName())) : nullptr;
		const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
		if (property != nullptr && property->GetObjectId() != 0 && metaData != nullptr) {

			// ⚠ DEFERRED, because the click is still being dispatched: opening a document from
			// inside the grid's own event tears down the cell that is handling it.
			wxTheApp->CallAfter(
				[id = property->GetObjectId(), metaData = *metaData]() {

					// ⭐⭐ JUST OPEN IT. Whose it is — the document holding that configuration, or
					// nobody's when it is the one the main window shows — is worked out INSIDE
					// OpenForm, from the object itself (Max, 2026-09-01: *"it is a very complicated
					// mechanism otherwise — you would have to track everywhere on whose behalf it
					// was opened"*).
					docManager->OpenForm(id, metaData);
				}
			);
		}
	}
}

void ibPGHyperLinkProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
