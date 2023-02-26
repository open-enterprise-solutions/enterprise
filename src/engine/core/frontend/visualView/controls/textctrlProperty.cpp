#include "widgets.h"

void CValueTextCtrl::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTextCtrl::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

#include <wx/propgrid/manager.h>

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

void CValueTextCtrl::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
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

bool CValueTextCtrl::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueTextCtrl::LoadFromVariant(newValue))
		return true;
	return IValueControl::OnPropertyChanging(property, newValue);
}