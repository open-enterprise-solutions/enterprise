////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type DescribeData
#include "backend/metaCollection/partial/commonObject.h"   // the owners an attribute asks: generic data, a hierarchy

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
// See docs/private/ownership-authority.md for why the facade exists at all.
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

// The empty answers — a field that is not chosen within anything. Statics, so the reference handed back
// outlives the call and every asker sees the same nothing (a predefined field, a common attribute).
const ibChoiceTypeLinkDescription& ibValueMetaObjectAttributeBase::GetTypeLink() const
{
	static const ibChoiceTypeLinkDescription s_none;
	return s_none;
}

const ibChoiceParametersDescription& ibValueMetaObjectAttributeBase::GetChoiceParameters() const
{
	static const ibChoiceParametersDescription s_none;
	return s_none;
}

// WHAT A VALUE HERE MAY BE — the type factory's answer (backend_type.cpp: a characteristic stands for its
// chart's types). Overridden here only because an attribute is both a type factory and a source column,
// and each base declares the question: one overrider answers for both, with the factory's answer.
ibTypeDescription& ibValueMetaObjectAttributeBase::GetTypeValueDesc() const
{
	return ibBackendTypeConfigFactory::GetTypeValueDesc();
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
	if (m_defValue.IsEmpty())
		return ibBackendTypeConfigFactory::CreateValue();
	return m_defValue;
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

// 🛑🛑 THIS LIST IS THE WHOLE OF WHAT SURVIVES. There is no generic walk of an object's properties:
// ibPropertyObject::ReadProperty / WriteProperty only route to attached objects, and every metatype
// names its own by hand. A property added to the class, given a variant, an editor, a dialog and a
// tool — and NOT added here — works perfectly in the session that set it and is gone at the next
// start. Nothing refuses, nothing warns; a read-back in the same session shows it set.
//
// MEASURED 2026-09-23: the link by type and the choice parameters were set, saved, applied, and read
// back EMPTY after the designer was restarted — so every battery over them narrowed nothing and
// cleared nothing, and it looked like the mechanism being broken rather than the value being absent.
// Whenever a property is added above, it is added HERE in the same change.
bool ibValueMetaObjectAttribute::ReadData(const ibDataNode& node)
{
	m_propertyType->SetNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->SetNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));
	m_propertyIndexingMode->SetNodeValue(node.GetProperty(m_propertyIndexingMode->GetName()));
	m_propertyItemMode->SetNodeValue(node.GetProperty(m_propertyItemMode->GetName()));
	m_propertySelectMode->SetNodeValue(node.GetProperty(m_propertySelectMode->GetName()));
	m_propertyTypeLink->SetNodeValue(node.GetProperty(m_propertyTypeLink->GetName()));
	m_propertyChoiceParameters->SetNodeValue(node.GetProperty(m_propertyChoiceParameters->GetName()));
	return true;
}
bool ibValueMetaObjectAttribute::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyType->GetName(),       m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(),  m_propertyFillCheck->GetNodeValue());
	node.SetProperty(m_propertyIndexingMode->GetName(),   m_propertyIndexingMode->GetNodeValue());
	node.SetProperty(m_propertyItemMode->GetName(),   m_propertyItemMode->GetNodeValue());
	node.SetProperty(m_propertySelectMode->GetName(), m_propertySelectMode->GetNodeValue());
	node.SetProperty(m_propertyTypeLink->GetName(),   m_propertyTypeLink->GetNodeValue());
	node.SetProperty(m_propertyChoiceParameters->GetName(), m_propertyChoiceParameters->GetNodeValue());
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