////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type DescribeData

////////////////////////////////////////////////////////////////////////////



//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"

bool ibValueMetaObjectAttributeBase::ContainType(const ibValueTypes& valType) const
{
	return GetTypeDesc().ContainType(valType);
}

bool ibValueMetaObjectAttributeBase::ContainType(const ibClassID& clsid) const
{
	return GetTypeDesc().ContainType(clsid);
}

bool ibValueMetaObjectAttributeBase::EqualType(const ibClassID& clsid, const ibTypeDescription& rhs) const
{
	return GetTypeDesc().EqualType(clsid, rhs);
}

bool ibValueMetaObjectAttributeBase::ContainMetaType(ibCtorObjectMetaType type) const
{
	for (auto& clsid : GetTypeDesc().GetClsidList()) {
		const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(clsid);
		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == type)
			return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////

ibItemMode ibValueMetaObjectAttribute::GetItemMode() const {
	ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject =
		dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_parent);
	if (metaObject != nullptr)
		return m_propertyItemMode->GetValueAsEnum();
	return ibItemMode::ibItemMode_Item;
}

ibSelectMode ibValueMetaObjectAttribute::GetSelectMode() const
{
	if (GetTypeDesc().GetClsidCount() > 1)
		return ibSelectMode::ibSelectMode_Items;
	const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(GetTypeDesc().GetFirstClsid());
	if (so != nullptr) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject = dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(so->GetMetaObject());
		if (so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference && metaObject != nullptr)
			return (ibSelectMode)m_propertySelectMode->GetValueAsInteger();
		return ibSelectMode::ibSelectMode_Items;
	}
	return ibSelectMode::ibSelectMode_Items;
}

/////////////////////////////////////////////////////////////////////////

ibSelectorDataType ibValueMetaObjectAttributeBase::GetFilterDataType() const
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	if (metaObject != nullptr) return metaObject->GetFilterDataType();
	return ibSelectorDataType::ibSelectorDataType_reference;
}

/////////////////////////////////////////////////////////////////////////

ibValue ibValueMetaObjectAttributeBase::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	if (refData == nullptr)
		return ibValue();
	return refData;
}

ibValue* ibValueMetaObjectAttributeBase::CreateValueRef() const
{
	if (m_defValue.IsEmpty()) 
		return ibBackendTypeConfigFactory::CreateValueRef();
	return new ibValue(m_defValue);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool ibValueMetaObjectAttributeBase::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAttributeBase::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectAttributeBase::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return ibValueMetaObject::OnReloadMetaObject();
	return false;
}

///////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectAttributeBase::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectAttributeBase::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0) OnReloadMetaObject();
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

// Per-type data = the attribute's properties, each serializing ITSELF into the
// node (self-naming via GetName(), typed where the property type overrides SaveTo):
// FillCheck is a readable Bool; ItemMode/Select/Type ride the base Binary bridge
// until their property types override (enum -> Int, Type -> Child sub-node).

bool ibValueMetaObjectAttribute::ReadData(const ibDataNode& node)
{
	m_propertyType->ReadNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->ReadNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));
	m_propertyIndexingMode->ReadNodeValue(node.GetProperty(m_propertyIndexingMode->GetName()));
	m_propertyItemMode->ReadNodeValue(node.GetProperty(m_propertyItemMode->GetName()));
	m_propertySelectMode->ReadNodeValue(node.GetProperty(m_propertySelectMode->GetName()));
	return true;
}
bool ibValueMetaObjectAttribute::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyType->GetName(),       m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(),  m_propertyFillCheck->GetNodeValue());
	node.SetProperty(m_propertyIndexingMode->GetName(),   m_propertyIndexingMode->GetNodeValue());
	node.SetProperty(m_propertyItemMode->GetName(),   m_propertyItemMode->GetNodeValue());
	node.SetProperty(m_propertySelectMode->GetName(), m_propertySelectMode->GetNodeValue());
	return true;
}


// Node form: the type descriptor as a Child (same readable shape as ibPropertyType,
// via the shared ibTypeDescriptionMemory::WriteNode) + the fill flag.
bool ibValueMetaObjectAttributePredefined::WriteData(ibDataNode& node) const
{
	ibDataValue typeValue;
	ibTypeDescriptionMemory::WriteNode(typeValue, m_typeDesc, GetMetaData());
	node.SetProperty(wxT("Type"), typeValue);
	node.SetValue(wxT("FillCheck"), (bool)m_fillCheck);
	node.SetValue(wxT("Indexing"), (int)m_indexingMode);
	return true;
}

bool ibValueMetaObjectAttributePredefined::ReadData(const ibDataNode& node)
{
	ibTypeDescriptionMemory::ReadNode(node.GetProperty(wxT("Type")), m_typeDesc, GetMetaData());
	m_fillCheck = node.GetValue<bool>(wxT("FillCheck"));
	m_indexingMode = (ibIndexingMode)node.GetValue<int>(wxT("Indexing"));   // absent -> 0 -> DontIndex
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAttribute, "Attribute", g_metaAttributeCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectAttributePredefined, "PredefinedAttribute", g_metaPredefinedAttributeCLSID);