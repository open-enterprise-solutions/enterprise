////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/metaData.h"

void IMetaObject::OnPropertyCreated(Property* property)
{
}

void IMetaObject::OnPropertySelected(Property* property)
{
}

bool IMetaObject::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	IBackendMetadataTree* metadataTree = m_metaData->GetMetaTree();
	if (m_propertyName == property && metadataTree != nullptr)
		return metadataTree->RenameMetaObject(this, newValue.GetString());
	else if (m_propertyName == property)
		return m_metaData->RenameMetaObject(this, newValue.GetString());

	m_metaData->Modify(true);
	return true;
}

#include "backend/backend_mainFrame.h"

void IMetaObject::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	wxASSERT(m_metaData);

	IBackendMetadataTree* metadataTree = m_metaData->GetMetaTree();
	if (backend_mainFrame && metadataTree != nullptr) {
		backend_mainFrame->RefreshFrame();
	}
}