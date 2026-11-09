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

	// How a reference is picked — only where the control holds a reference.
	bool hasReference = false;
	for (const ibClassID& clsid : GetTypeDesc().GetClsidList())
		hasReference = hasReference || ::IsReference(clsid);
	HideProperty(m_propertyQuickChoice, !hasReference);
	HideProperty(m_propertyChoiceFoldersAndItems, !hasReference);
	// …and what narrows its list (#616). The link by type stays, as an attribute's does: a field is typed before the
	// field that will type it is thought of, and a row that comes and goes with that order cannot be found.
	HideProperty(m_propertyChoiceParameters, !hasReference);
}

bool ibValueTextCtrl::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	//if (m_propertySource == property && !ibValueTextCtrl::LoadFromVariant(newValue))
	//	return true;
	return ibValueControl::OnPropertyChanging(property, newValue);
}