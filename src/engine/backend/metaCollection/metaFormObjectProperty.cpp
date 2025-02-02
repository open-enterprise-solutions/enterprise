////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform property
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/appData.h"

void CMetaObjectForm::OnPropertyCreated(Property* property)
{
}

void CMetaObjectForm::OnPropertySelected(Property* property)
{
}

void CMetaObjectForm::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (property == m_properyFormType) {
		if (appData->DesignerMode()) {
			IModuleManager* moduleManager = m_metaData->GetModuleManager();
			wxASSERT(moduleManager);
			IMetaObjectGenericData* metaObjectValue = wxDynamicCast(m_parent, IMetaObjectGenericData);
			wxASSERT(metaObjectValue);
			IBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
			if (metaTree != nullptr) {
				metaTree->CloseMetaObject(this);
			}
			if (moduleManager->RemoveCompileModule(this)) {
				moduleManager->AddCompileModule(this, formWrapper::inl::cast_value(metaObjectValue->CreateObjectForm(this)));
			}
			if (metaTree != nullptr) {
				metaTree->UpdateChoiceSelection();
			}
		}
	}

	m_metaData->Modify(true);
}