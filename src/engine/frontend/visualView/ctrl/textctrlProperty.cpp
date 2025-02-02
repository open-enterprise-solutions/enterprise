#include "widgets.h"

void CValueTextCtrl::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTextCtrl::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

#include <wx/propgrid/manager.h>

#include "backend/metaData.h"
#include "backend/objCtor.h"

void CValueTextCtrl::OnPropertyRefresh(wxPropertyGridManager* pg, wxPGProperty* pgProperty, Property* property)
{
	if (m_propertyChoiceForm == property) {
		if (GetClsidCount() > 1) {
			pg->HideProperty(pgProperty, true);
		}
		else {
			IMetaValueTypeCtor* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
			if (so != nullptr) {
				if (so->GetMetaTypeCtor() != eCtorMetaType::eCtorMetaType_Reference)
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