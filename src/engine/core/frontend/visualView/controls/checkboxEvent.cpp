#include "widgets.h"
#include "form.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metadata.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void CValueCheckbox::OnClickedCheckbox(wxCommandEvent& event)
{
	wxCheckBox* checkbox =
		wxDynamicCast(event.GetEventObject(), wxCheckBox);

	m_selValue = checkbox->GetValue();

	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcData = m_formOwner->GetSourceObject();
		wxASSERT(srcData);
		srcData->SetValueByMetaID(GetIdByGuid(m_dataSource), m_selValue);
	}

	event.Skip(
		CallEvent(m_onCheckboxClicked)
	);
}