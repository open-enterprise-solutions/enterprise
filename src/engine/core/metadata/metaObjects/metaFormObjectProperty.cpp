////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform property
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "frontend/visualView/visualHost.h"
#include "metadata/metadata.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "appData.h"

void CMetaFormObject::OnPropertyCreated(Property *property)
{
}

void CMetaFormObject::OnPropertySelected(Property *property)
{
}

void CMetaFormObject::OnPropertyChanged(Property *property)
{
	if (property->GetName() == wxT("form_type")) {
		if (appData->DesignerMode()) {		
			IModuleManager *moduleManager = m_metaData->GetModuleManager();
			wxASSERT(moduleManager);
			IMetaObjectWrapperData *metaObjectValue = wxStaticCast(m_parent, IMetaObjectWrapperData);
			wxASSERT(metaObjectValue);
			IMetadataTree *metaTree = m_metaData->GetMetaTree();		
			if (metaTree) {
				metaTree->CloseMetaObject(this);
			}
			if (moduleManager->RemoveCompileModule(this)) {
				moduleManager->AddCompileModule(this, metaObjectValue->CreateObjectValue(this));
			}
			if (metaTree) {
				metaTree->OnPropertyChanged();
			}
		}
	}

	m_metaData->Modify(true);
}