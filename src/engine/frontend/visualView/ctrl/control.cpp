////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueControl, ibValueFrame)

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

ibValueControl::ibValueControl()
	: ibValueFrame(), m_formOwner(nullptr)
{
}

ibValueControl::~ibValueControl()
{
	SetOwnerForm(nullptr);
}

#include "backend/metaData.h"

void ibValueControl::SetOwnerForm(ibValueForm* ownerForm)
{
	// Just record the owner. The form derives its control list by walking the
	// hierarchy (ibValueForm::GetControlList) — there is no maintained set, so this
	// no longer touches the form. Removes the teardown hazard where ~ibValueControl
	// erased from an already-destroyed m_listControl.
	m_formOwner = ownerForm;
}

ibMetaData* ibValueControl::GetMetaData() const
{
	const ibValueMetaObjectFormBase* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() : nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ibSourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			const ibValueMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

ibFormID ibValueControl::GetTypeForm() const
{
	if (m_formOwner == nullptr) {
		wxASSERT(m_formOwner);
		return 0;
	}

	const ibValueMetaObjectFormBase* creator = m_formOwner->GetFormMetaObject();
 	if (creator != nullptr) 
		return creator->GetTypeForm();
	return m_formOwner->GetTypeForm();
}