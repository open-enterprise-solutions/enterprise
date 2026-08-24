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

wxString ibPGHyperLinkProperty::ValueToString( wxVariant& value, wxPGPropValFormatFlags flags ) const
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
			// ⭐⭐ THE ID AND THE CONFIGURATION CROSS THE HOP, NOT THE OBJECT. A deferred call runs after
			// the click has been dispatched, and between the two a metaobject can be deleted or the
			// configuration reloaded — a raw pointer captured here would then be read after it died
			// (audit, 2026-08-24; the same family as the two designer dumps this cell already carries a
			// note about, and the name-stamp fix cured the repeat firing, not this).
			//
			// An id is a fact that outlives objects: it is re-resolved on the other side, and a
			// metaobject that has gone simply is not found.
			ibMetaData* metaData = metaObject->GetMetaData();
			const ibMetaID metaId = metaObject->GetMetaID();
			if (metaData != nullptr) {
				wxTheApp->CallAfter(
					[metaData, metaId]() {
						// ASKED OF THE CONFIGURATION ITSELF — `ibMetaData::FindAnyObjectByFilter` is the
						// same walk the root would do, one call earlier. Going through
						// `GetCommonMetaObject()` bought nothing and needed its result narrowed to the
						// configuration class, which is not what that accessor hands back.
						ibValueMetaObject* found = metaData->FindAnyObjectByFilter(metaId);
						if (found == nullptr)
							return;   // deleted or reloaded while the click was in flight
						ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
						if (metaTree != nullptr) metaTree->OpenObjectForm(found);
					}
				);
			}
		}
	}
}

void ibPGHyperLinkProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
