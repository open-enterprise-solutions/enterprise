////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "form.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueControl, IValueFrame)

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueControl::IValueControl()
	: IValueFrame(), m_formOwner(NULL)
{
}

IValueControl::~IValueControl()
{
	SetOwnerForm(NULL);
}

IValueControl *IValueControl::GetChild(unsigned int idx)
{
	return dynamic_cast<IValueControl *>(IObjectBase::GetChild(idx));
}

IValueControl *IValueControl::GetChild(unsigned int idx, const wxString &type)
{
	return dynamic_cast<IValueControl *>(IObjectBase::GetChild(idx, type));
}

#include "metadata/metadata.h"

void IValueControl::SetOwnerForm(CValueForm *ownerForm)
{
	if (ownerForm && !m_formOwner) {
		wxString className = GetClassName();
		if (className != wxT("sizerItem")) {
			ownerForm->m_aControls.push_back(this);
		}
	}
	else if (!ownerForm && m_formOwner) {
		auto foundedIt = std::find(
			m_formOwner->m_aControls.begin(),
			m_formOwner->m_aControls.end(),
			this
		);
		if (foundedIt != m_formOwner->m_aControls.end()) {
			m_formOwner->m_aControls.erase(foundedIt);
		}
	}

	m_formOwner = ownerForm;
}

IMetadata *IValueControl::GetMetaData() const
{
	IMetaFormObject *metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		NULL;

	//for form buider
	if (metaFormObject == NULL) {
		IDataObjectSource *srcValue = m_formOwner->GetSourceObject();
		if (srcValue) {
			IMetaObjectValue *metaValue = srcValue->GetMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetadata();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetadata() :
		NULL;
}

OptionList *IValueControl::GetTypelist() const
{
	IMetadata *metaData = GetMetaData();

	return metaData ?
		metaData->GetTypelist() :
		NULL;
}

#include "metadata/metaObjects/metaFormObject.h"

form_identifier_t IValueControl::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	IMetaFormObject *metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);
	return metaFormObj->GetTypeForm();
}

CProcUnit *IValueControl::GetFormProcUnit() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return NULL;
	}

	return m_formOwner->GetProcUnit();
}