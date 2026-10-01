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
			// ⭐ THE FORM THAT WAS IS GONE, AND A NEW ONE STANDS IN ITS PLACE — which is exactly the
			// pair of stages the two lines below perform on the compile cache. Said as those stages,
			// this needs no verb of its own: a watcher shuts the editor showing the old form on
			// `Removed`, and `Created` asks nothing of it (the kind is already chosen, so the
			// configuration tree's form-kind dialog is skipped by its own guard).
			//
			// 🛑 It used to call CloseMetaObject, which is a verb for something that is going away
			// for good. This form is not: it is being rebuilt, and saying the wrong one meant every
			// watcher had to guess which of the two had happened.
			m_metaData->MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Removed, this);
			if (cc->RemoveCompileModule(this)) {
				// Keyed by the METAFORM — the cache's own value (see CreateObjectForm).
				cc->AddCompileModule(this, formWrapper::inl::cast_value(metaObjectValue->CreateObjectForm(this)));
			}
			m_metaData->MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Created, this);
		}
	}

	ibValueMetaObjectFormBase::OnPropertyChanged(property, oldValue, newValue);
}