#include "advpropHyperLink.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGHyperLinkProperty, wxPGProperty, HyperLink)

#include "backend/metaCollection/metaObject.h"

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

#include "backend/metaData.h"
#include "frontend/docView/docView.h"   // docManager — the door that opens a metaobject

void ibPGHyperLinkProperty::OnSetValue()
{
	// THE NAME IS THE CLICK. The editor stamps the name onto whatever the cell carries and does not
	// set a bool any more (propertyEditor.cpp), so a `GetBool()` here would never be true again —
	// this cell's own value is `false` from its constructor.
	if (wxT("hyperLink_clicked") == m_value.GetName()) {
		// ⭐ TAKEN, SO ENDED — the same rule the composer's and the list's cells follow. A name left
		// standing turns one click into every later refresh.
		m_value.SetName(wxEmptyString);

		ibValueMetaObject* metaObject = dynamic_cast<ibValueMetaObject*>(m_ownerProperty);
		if (metaObject != nullptr) {

			// ⚠ DEFERRED, because the click is still being dispatched: opening a document from
			// inside the grid's own event tears down the cell that is handling it.
			wxTheApp->CallAfter(
				[metaObject]() {

					// ⭐⭐ JUST OPEN IT. Whose it is — the document holding that configuration, or
					// nobody's when it is the one the main window shows — is worked out INSIDE
					// OpenForm, from the object itself (Max, 2026-09-01: *"it is a very complicated
					// mechanism otherwise — you would have to track everywhere on whose behalf it
					// was opened"*).
					//
					// 🛑 It used to pass no owner and mean it, so a module of an external data
					// processor landed in the manager's top level instead of under that processor's
					// document. The editor appeared, which is why it looked right; everything asked
					// afterwards was wrong, because "is this already open?" is asked of the owner.
					docManager->OpenForm(metaObject, ibDOC_NEW);
				}
			);
		}
	}
}

void ibPGHyperLinkProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
