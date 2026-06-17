#include "widgets.h"
#include "form.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueCheckbox::OnClickedCheckbox(wxCommandEvent& event)
{
	// Both wx (desktop wxCheckBox) and our web path set event.GetInt()
	// to the new checked state — read from there to keep a single
	// handler body across platforms, no dynamic_cast to a native class
	// that isn't present in the web build.
	m_selValue = event.GetInt() != 0;

	if (!m_propertySource->IsEmptyProperty()) {
		ibSourceDataObject* srcData = m_formOwner->GetSourceObject();
		wxASSERT(srcData);
		// A dotted reference path is read-only — only a single-column binding writes back.
		if (srcData != nullptr && !m_propertySource->IsDotWalk())
			srcData->SetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
	}

	m_formOwner->RefreshForm();

	event.Skip(
		CallAsEvent(m_onCheckboxClicked)
	);
}