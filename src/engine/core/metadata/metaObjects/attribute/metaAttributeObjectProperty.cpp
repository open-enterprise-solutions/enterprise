////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

void CMetaAttributeObject::OnPropertyCreated(Property* property)
{
	if (m_propertyType == property) {
		CMetaAttributeObject::SaveToVariant(m_propertyType->GetValue(), m_metaData);
	}
}

#include <wx/propgrid/manager.h>

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

void CMetaAttributeObject::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertySelectMode == property) {
		if (GetClsidCount() > 1) {
			pg->HideProperty(pgProperty, true);
		}
		else {
			IMetaTypeObjectValueSingle* so = GetMetadata()->GetTypeObject(GetFirstClsid());
			if (so != NULL) {
				IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(so->GetMetaObject());
				if (metaObject == NULL)
					pg->HideProperty(pgProperty, true);
				else if (so->GetMetaType() != eMetaObjectType::enReference)
					pg->HideProperty(pgProperty, true);
				else 
					pg->HideProperty(pgProperty, false);
			}
			else {
				pg->HideProperty(pgProperty, true);
			}
		}
	}
	else if (m_propertyItemMode == property) {
		IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(m_parent);
		pg->HideProperty(pgProperty, metaObject == NULL);
	}
}

bool CMetaAttributeObject::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyType == property && !CMetaAttributeObject::LoadFromVariant(newValue))
		return false;
	return IMetaAttributeObject::OnPropertyChanging(property, newValue);
}

void CMetaAttributeObject::OnPropertyChanged(Property* property)
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property);
	}
}