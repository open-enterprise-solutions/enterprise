#include "widgets.h"
#include "form.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/metaData.h"

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
		CallAsEvent(m_onCheckboxClicked)
	);
}