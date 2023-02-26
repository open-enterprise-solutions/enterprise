////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"

#include <wx/propgrid/manager.h>

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

void CMetaTableObject::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertyUse == property) {
		IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(m_parent);
		pg->HideProperty(pgProperty, metaObject == NULL);
	}
}