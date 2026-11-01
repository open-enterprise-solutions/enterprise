#include "tableBox.h"

void ibValueModelTableBoxColumn::OnPropertyCreated(ibProperty* property)
{
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueModelTableBoxColumn::OnPropertyRefresh()
{
	ibValueControl::OnPropertyRefresh();

	// A choice form can only be offered when the column points at ONE reference type.
	bool hasChoice = false;
	if (GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
		hasChoice = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference;
	}
	HideProperty(m_propertyChoiceForm, !hasChoice);

	// How a reference is picked, and what is shown to pick it from — only where the column holds a reference.
	bool hasReference = false;
	for (const ibClassID& clsid : GetTypeDesc().GetClsidList())
		hasReference = hasReference || ::IsReference(clsid);
	HideProperty(m_propertyQuickChoice, !hasReference);
	HideProperty(m_propertyChoiceFoldersAndItems, !hasReference);
	HideProperty(m_propertyChoiceParameters, !hasReference);
}

bool ibValueModelTableBoxColumn::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueControl::OnPropertyChanging(property, newValue);
}