////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type DescribeData
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"   // a characteristic answers with its chart's list

////////////////////////////////////////////////////////////////////////////



//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"

// ⭐ THE QUERY FACE, ASKING ITS OWNER — the two answers that cannot be one-liners in the header,
// because the interface hands back a REFERENCE and a detached facade has no owner to borrow one
// from. It answers with a shared empty description instead of a dangling one: a column that outlived
// its attribute has no type, which is a fact, and saying it costs nothing. (Nothing may write through
// a type description a column hands out — that is already true of every one of them.)
// See docs/ownership-authority.md for why the facade exists at all.
static ibTypeDescription& ibDetachedColumnTypeDesc()
{
	static ibTypeDescription s_none;
	return s_none;
}

ibTypeDescription& ibValueMetaObjectAttributeBase::ibMetaAttributeColumn::GetTypeDesc() const
{
	return m_owner != nullptr ? m_owner->GetTypeDesc() : ibDetachedColumnTypeDesc();
}

ibTypeDescription& ibValueMetaObjectAttributeBase::ibMetaAttributeColumn::GetTypeValueDesc() const
{
	return m_owner != nullptr ? m_owner->GetTypeValueDesc() : ibDetachedColumnTypeDesc();
}

// WHAT A VALUE HERE MAY BE. Ordinary declarations answer with themselves; a characteristic answers
// with the list its CHART declares — the owner keeps it, the field borrows it, so a chart that gains
// a type widens every field declared through it at once, with nothing copied or recomputed.
//
// The chart is reached through the registry this attribute already belongs to — its own configuration,
// never the active one. An unresolved chart (not loaded yet) leaves the declaration standing.
ibTypeDescription& ibValueMetaObjectAttributeBase::GetTypeValueDesc() const
{
	ibTypeDescription& declared = GetTypeDesc();
	if (m_metaData == nullptr || declared.GetClsidCount() != 1 || !IsCharacteristic(declared.GetFirstClsid()))
		return declared;

	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(declared.GetFirstClsid());
	const ibValueMetaObjectChartOfCharacteristicTypes* chart = nullptr;
	if (typeCtor == nullptr || typeCtor->GetMetaObject() == nullptr ||
		!typeCtor->GetMetaObject()->ConvertToValue(chart) || chart == nullptr)
		return declared;

	// The chart's list, BORROWED: the owner keeps it, the field only points at it.
	//
	// Non-const on purpose. Reaching it THROUGH THE METADATA is the legal way to get at a live
	// declaration — a configuration is edited, so its type descriptions are state, not a frozen
	// snapshot. Constifying the borrow here would only force a cast at the first editor that needs it.
	return chart->GetTypesOfCharacteristics();
}

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
	m_propertyType->SetNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->SetNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));
	m_propertyIndexingMode->SetNodeValue(node.GetProperty(m_propertyIndexingMode->GetName()));
	m_propertyItemMode->SetNodeValue(node.GetProperty(m_propertyItemMode->GetName()));
	m_propertySelectMode->SetNodeValue(node.GetProperty(m_propertySelectMode->GetName()));
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


// ⭐⭐ A PREDEFINED ATTRIBUTE HAS NOTHING TO SERIALISE — that is what makes it predefined.
//
// The pair below used to write the type, the fill-check and the indexing, and that is INHERITED
// SHAPE, not a decision: a predefined attribute began life as a variety of the ordinary one, whose
// same three values ARE editable and therefore have to be stored. It stopped being that kind of
// attribute; the storage stayed.
//
// Its shape comes from the METATYPE: the constructor states the type, the fill-check and the
// indexing, and every configuration that opens tomorrow gets the same answers, because the answers
// are in the code. Writing them into the configuration turned the declaration into a mere INITIAL
// value — the first save froze whatever the platform said that day, and from then on the file won,
// silently, forever.
//
// It cost exactly that. `AccountType` was declared fill-checked; every configuration saved before
// that declaration existed simply had no `FillCheck` node, `GetValue<bool>` on a missing node is
// `false`, and the read handed that `false` straight over the top of the `true` the constructor had
// just set. An account with no side saved without a word, and no amount of looking at the write path
// could show why: the flag was correct at construction and wrong one load later.
//
// The types that DEPEND on a setting are not stored either, and never needed to be: Parent, Owner,
// Account, AccountCr and the dimension slots are all re-typed from their bindings by
// `SetDefaultMetaType` in the run phase AND at the property change (see catalogMetadata.cpp:298,
// accountingRegisterMetadata.cpp:659, chartOfAccountsMetadata.cpp:324) — which is precisely why
// those calls sit in both places. Nothing else about the attribute is editable: `SetSynonym` is
// inert, and the two owner-driven exceptions (`SetSelectMode`, `SetOwnerSynonym`) are restated by
// the owner on every load for the same reason.
//
// Old configurations still carry the nodes; they are simply ignored now.
bool ibValueMetaObjectAttributePredefined::WriteData(ibDataNode& node) const
{
	return true;
}

bool ibValueMetaObjectAttributePredefined::ReadData(const ibDataNode& node)
{
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAttribute, "Attribute", g_metaAttributeCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectAttributePredefined, "PredefinedAttribute", g_metaPredefinedAttributeCLSID);