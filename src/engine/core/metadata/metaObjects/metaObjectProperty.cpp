////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "metadata/metadata.h"
#include "utils/typeconv.h"

void IMetaObject::OnPropertyCreated(Property* property)
{
}

void IMetaObject::OnPropertySelected(Property* property)
{
}

#include "common/docInfo.h"
#include "common/docManager.h"

void IMetaObject::OnPropertyChanged(Property* property)
{
	wxASSERT(m_metaData); 
	m_metaData->Modify(true);
	IMetadataWrapperTree* metaTree = m_metaData->GetMetaTree();
	if (metaTree != NULL) {
		for (auto doc : docManager->GetDocumentsVector()) {
			doc->UpdateAllViews();
		}
	}

}