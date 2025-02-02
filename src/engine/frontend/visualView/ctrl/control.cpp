////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueControl, IValueFrame)

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueControl::IValueControl()
	: IValueFrame(), m_formOwner(nullptr)
{
}

IValueControl::~IValueControl()
{
	SetOwnerForm(nullptr);
}

IValueControl* IValueControl::GetChild(unsigned int idx) const
{
	return dynamic_cast<IValueControl*>(IPropertyObject::GetChild(idx));
}

IValueControl* IValueControl::GetChild(unsigned int idx, const wxString& type) const
{
	return dynamic_cast<IValueControl*>(IPropertyObject::GetChild(idx, type));
}

#include "backend/metaData.h"

void IValueControl::SetOwnerForm(CValueForm* ownerForm)
{
	if (ownerForm && m_formOwner == nullptr) {
		if (GetComponentType() != COMPONENT_TYPE_SIZERITEM)
			ownerForm->m_aControls.push_back(this);
	}
	else if (!ownerForm && m_formOwner != nullptr) {
		auto& it = std::find(
			m_formOwner->m_aControls.begin(),
			m_formOwner->m_aControls.end(),
			this
		);
		if (it != m_formOwner->m_aControls.end())
			m_formOwner->m_aControls.erase(it);
	}
	m_formOwner = ownerForm;
}

IMetaData* IValueControl::GetMetaData() const
{
	IMetaObjectForm* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ISourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			IMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

form_identifier_t IValueControl::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	IMetaObjectForm* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);
	return metaFormObj->GetTypeForm();
}

CProcUnit* IValueControl::GetFormProcUnit() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return nullptr;
	}

	return m_formOwner->GetProcUnit();
}