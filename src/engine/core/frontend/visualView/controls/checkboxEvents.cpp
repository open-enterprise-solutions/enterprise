#include "widgets.h"
#include "form.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "metadata/metadata.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void CValueCheckbox::OnClickedCheckbox(wxCommandEvent& event)
{
	wxCheckBox *checkbox =
		wxDynamicCast(event.GetEventObject(), wxCheckBox);

	m_selValue = checkbox->GetValue();

	if (m_dataSource != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		ISourceDataObject *srcData = m_formOwner->GetSourceObject();
		wxASSERT(srcData);
		srcData->SetValueByMetaID(m_dataSource, m_selValue);
	}

	event.Skip(CallEvent("onCheckboxClicked"));
}