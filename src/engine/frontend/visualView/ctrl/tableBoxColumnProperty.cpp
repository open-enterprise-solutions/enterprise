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
}

bool ibValueModelTableBoxColumn::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return ibValueControl::OnPropertyChanging(property, newValue);
}