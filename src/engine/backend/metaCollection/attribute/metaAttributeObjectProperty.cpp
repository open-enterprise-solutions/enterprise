////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

void CMetaObjectAttribute::OnPropertyCreated(Property* property)
{
	if (m_propertyType == property) {
		CMetaObjectAttribute::SaveToVariant(m_propertyType->GetValue(), m_metaData);
	}
}

#include <wx/propgrid/manager.h>

void CMetaObjectAttribute::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertySelectMode == property) {
		if (GetClsidCount() > 1) {
			pg->HideProperty(pgProperty, true);
		}
		else {
			IMetaValueTypeCtor* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
			if (so != nullptr) {
				IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(so->GetMetaObject());
				if (metaObject == nullptr)
					pg->HideProperty(pgProperty, true);
				else if (so->GetMetaTypeCtor() != eCtorMetaType::eCtorMetaType_Reference)
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
		pg->HideProperty(pgProperty, metaObject == nullptr);
	}
}

bool CMetaObjectAttribute::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyType == property && !CMetaObjectAttribute::LoadFromVariant(newValue))
		return false;
	return IMetaObjectAttribute::OnPropertyChanging(property, newValue);
}

void CMetaObjectAttribute::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property, oldValue, newValue);
	}
}