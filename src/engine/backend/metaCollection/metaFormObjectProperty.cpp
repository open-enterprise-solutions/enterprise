////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform property
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/appData.h"

void ibValueMetaObjectForm::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectFormBase::OnPropertyCreated(property);
}

void ibValueMetaObjectForm::OnPropertySelected(ibProperty* property)
{
	ibValueMetaObjectFormBase::OnPropertySelected(property);
}

void ibValueMetaObjectForm::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (property == m_properyFormType) {
		if (auto* cc = m_metaData->GetCompileCache()) {
			ibValueMetaObjectGenericData* metaObjectValue = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
			wxASSERT(metaObjectValue);
			if (auto* metaTree = m_metaData->GetMetaTree())
				metaTree->CloseMetaObject(this);
			if (cc->RemoveCompileModule(this)) {
				cc->AddCompileModule(this, formWrapper::inl::cast_value(metaObjectValue->CreateObjectForm(this)));
			}
			if (auto* metaTree = m_metaData->GetMetaTree())
				metaTree->UpdateChoiceSelection();
		}
	}

	ibValueMetaObjectFormBase::OnPropertyChanged(property, oldValue, newValue);
}