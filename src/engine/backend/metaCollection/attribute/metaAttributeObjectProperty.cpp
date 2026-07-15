////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectAttribute::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertyType == property) {
	/////	ibValueMetaObjectAttribute::SaveToVariant(m_propertyType->GetValue(), m_metaData);
	//}
}

void ibValueMetaObjectAttribute::OnPropertyRefresh()
{
	ibValueMetaObjectAttributeBase::OnPropertyRefresh();

	// SelectMode only means something for an attribute that points at ONE hierarchical
	// reference type: a multi-type attribute has no single hierarchy to choose in.
	bool selectable = false;
	if (GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
		selectable = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference
			&& dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(so->GetMetaObject()) != nullptr;
	}
	HideProperty(m_propertySelectMode, !selectable);

	// ItemMode is the OWNER's question — only a hierarchical owner has folders vs items.
	HideProperty(m_propertyItemMode,
		dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_parent) == nullptr);
}

bool ibValueMetaObjectAttribute::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	//if (m_propertyType == property && !ibValueMetaObjectAttribute::LoadFromVariant(newValue))
	//	return false;
	return ibValueMetaObjectAttributeBase::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectAttribute::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
	}
}