////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "controlInterface.h"
#include "form.h"
#include "utils/stringUtils.h"

IValueFrame* IValueFrame::DoFindControlByName(const wxString& controlName, IValueFrame* top)
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IValueFrame* child = top->GetChild(idx);
		wxASSERT(child);
		IValueFrame* foundedMeta = DoFindControlByName(controlName, child);
		if (foundedMeta) {
			return foundedMeta;
		}
	}

	if (StringUtils::CompareString(controlName, top->GetControlName())) {
		return top;
	}

	return NULL;
}

IValueFrame* IValueFrame::FindControlByName(const wxString& controlName)
{
	return DoFindControlByName(controlName, GetOwnerForm());
}

void IValueFrame::DoGenerateNewID(form_identifier_t& id, IValueFrame* top)
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IValueFrame* child = top->GetChild(idx);
		wxASSERT(child);
		form_identifier_t newID = child->GetControlID() + 1;
		if (newID > id) {
			id = newID;
		}
		DoGenerateNewID(id, child);
	}
}

form_identifier_t IValueFrame::GenerateNewID()
{
	wxASSERT(m_controlId == 0);
	CValueForm* ownerForm = GetOwnerForm();
	wxASSERT(ownerForm);
	form_identifier_t id = ownerForm->GetControlID() + 1; // 1 is valueForm  
	DoGenerateNewID(id, ownerForm);
	m_controlId = id;
	return id;
}

IValueFrame* IValueFrame::DoFindControlByID(const form_identifier_t& id, IValueFrame* top)
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IValueFrame* child = top->GetChild(idx);
		wxASSERT(child);
		IValueFrame* foundedMeta = DoFindControlByID(id, child);
		if (foundedMeta) {
			return foundedMeta;
		}
	}

	if (id == top->GetControlID()) {
		return top;
	}

	return NULL;
}

IValueFrame* IValueFrame::FindControlByID(const form_identifier_t& id)
{
	return DoFindControlByID(id, GetOwnerForm());
}