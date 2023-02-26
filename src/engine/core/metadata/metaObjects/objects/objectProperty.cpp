#include "object.h"

void IMetaObjectRecordDataMutableRef::OnPropertyCreated(Property* property)
{
	if (m_propertyGeneration == property) {
		m_genData.SaveToVariant(m_propertyGeneration->GetValue(), m_metaData);
	}
	IMetaObjectRecordDataRef::OnPropertyCreated(property);
}

#include <wx/propgrid/manager.h>

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

void IMetaObjectRecordDataMutableRef::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertyQuickChoice == property) {
		pg->HideProperty(pgProperty, true);
	}
}

bool IMetaObjectRecordDataMutableRef::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyGeneration == property && !m_genData.LoadFromVariant(newValue))
		return true;
	return IMetaObjectRecordDataRef::OnPropertyChanging(property, newValue);
}

void IMetaObjectRecordDataMutableRef::OnPropertyChanged(Property* property)
{
	IMetaObjectRecordDataRef::OnPropertyChanged(property);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void IMetaObjectRecordDataFolderMutableRef::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
}