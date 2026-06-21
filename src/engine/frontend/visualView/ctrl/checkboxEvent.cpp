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

	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		// Form writes only a direct-field binding (head selects the attribute); a
		// dotted reference path is read-only → no-op.
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsPath(), m_selValue);
	}

	m_formOwner->RefreshForm();

	event.Skip(
		CallAsEvent(m_onCheckboxClicked)
	);
}