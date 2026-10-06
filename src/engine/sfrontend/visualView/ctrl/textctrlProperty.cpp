#include "widgets.h"

void ibValueTextCtrl::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertySource == property) {
	//	ibValueTextCtrl::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	//}
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueTextCtrl::OnPropertyRefresh()
{
	ibValueControl::OnPropertyRefresh();

	// A choice form can only be offered when the control points at ONE reference type.
	bool hasChoice = false;
	if (GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
		hasChoice = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference;
	}
	HideProperty(m_propertyChoiceForm, !hasChoice);
}

bool ibValueTextCtrl::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	//if (m_propertySource == property && !ibValueTextCtrl::LoadFromVariant(newValue))
	//	return true;
	return ibValueControl::OnPropertyChanging(property, newValue);
}