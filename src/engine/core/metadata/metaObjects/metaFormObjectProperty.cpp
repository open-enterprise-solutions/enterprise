////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform property
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "frontend/visualView/visualHost.h"
#include "core/metadata/metadata.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "appData.h"

void CMetaFormObject::OnPropertyCreated(Property *property)
{
}

void CMetaFormObject::OnPropertySelected(Property *property)
{
}

void CMetaFormObject::OnPropertyChanged(Property *property)
{
	if (property == m_properyFormType) {
		if (appData->DesignerMode()) {		
			IModuleManager *moduleManager = m_metaData->GetModuleManager();
			wxASSERT(moduleManager);
			IMetaObjectWrapperData *metaObjectValue = wxStaticCast(m_parent, IMetaObjectWrapperData);
			wxASSERT(metaObjectValue);
			IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();		
			if (metaTree != NULL) {
				metaTree->CloseMetaObject(this);
			}
			if (moduleManager->RemoveCompileModule(this)) {
				moduleManager->AddCompileModule(this, metaObjectValue->CreateObjectForm(this));
			}
			if (metaTree != NULL) {
				metaTree->UpdateChoiceSelection();
			}
		}
	}

	m_metaData->Modify(true);
}