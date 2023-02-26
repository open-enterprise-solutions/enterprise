////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "core/metadata/metadata.h"
#include "utils/typeconv.h"

void IMetaObject::OnPropertyCreated(Property* property)
{
}

void IMetaObject::OnPropertySelected(Property* property)
{
}

bool IMetaObject::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	IMetadataWrapperTree* metadataTree = m_metaData->GetMetaTree();
	if (m_propertyName == property && metadataTree != NULL)
		return metadataTree->RenameMetaObject(this, newValue.GetString());
	else if (m_propertyName == property)
		return m_metaData->RenameMetaObject(this, newValue.GetString());

	m_metaData->Modify(true);
	return true;
}

#include "frontend/docView/docView.h"
#include "core/frontend/docView/docManager.h"

void IMetaObject::OnPropertyChanged(Property* property)
{
	wxASSERT(m_metaData); 
	IMetadataWrapperTree* metadataTree = m_metaData->GetMetaTree();
	if (metadataTree != NULL) {
		for (auto doc : docManager->GetDocumentsVector()) {
			doc->UpdateAllViews();
		}
	}
}