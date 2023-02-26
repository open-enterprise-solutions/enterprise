#include "tableBox.h"

void CValueTableBoxColumn::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTableBoxColumn::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

#include <wx/propgrid/manager.h>

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

void CValueTableBoxColumn::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertyChoiceForm == property) {
		if (GetClsidCount() > 1) {
			pg->HideProperty(pgProperty, true);
		}
		else {
			IMetaTypeObjectValueSingle* so = GetMetadata()->GetTypeObject(GetFirstClsid());
			if (so != NULL) {
				if (so->GetMetaType() != eMetaObjectType::enReference)
					pg->HideProperty(pgProperty, true);
				else
					pg->HideProperty(pgProperty, false);
			}
			else {
				pg->HideProperty(pgProperty, true);
			}
		}
	}
}

bool CValueTableBoxColumn::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueTableBoxColumn::LoadFromVariant(newValue))
		return false;
	return IValueControl::OnPropertyChanging(property, newValue);
}