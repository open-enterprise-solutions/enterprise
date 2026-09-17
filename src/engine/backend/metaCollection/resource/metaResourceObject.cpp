#include "metaResourceObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaCollection/partial/accountingRegister.h"   // the one owner that names a chart

// THE OWNER IS ASKED WHAT IT IS — see the note beside the declaration; a dimension answers it the same
// way, because the question is the same one.
const ibValueMetaObjectChartOfAccounts* ibValueMetaObjectResource::GetChartOfAccounts() const
{
	const ibValueMetaObjectAccountingRegister* reg = ibValueMetaObjectAccountingRegister::OwnerOf(m_parent);
	return reg != nullptr ? reg->GetChartOfAccounts() : nullptr;
}


// A figure kept per side takes its sides with it when it goes — the same rule and the same reason as a
// dimension's (metaDimensionObject.cpp).
bool ibValueMetaObjectResource::OnDeleteMetaObject()
{
	if (ibValueMetaObjectAccountingRegister* reg = ibValueMetaObjectAccountingRegister::OwnerOf(m_parent))
		reg->SyncFieldSides(this);
	return ibValueMetaObjectAttribute::OnDeleteMetaObject();
}

ibSelectorDataType ibValueMetaObjectResource::GetFilterDataType() const
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	if (metaObject->GetClassType() == g_metaInformationRegisterCLSID)
		return metaObject->GetFilterDataType();
	return ibSelectorDataType::ibSelectorDataType_resource;
}

// ⚠ AN ABSENT PROPERTY IS NOT A `false`. Every configuration written before this flag existed has no
// node for it, and reading one anyway hands the property an EMPTY value — which a boolean reads as
// "no". The default here is TRUE (a posting balances in its amount, § the declaration above), so a
// blind read turned every stored resource of every existing configuration into a non-balance one:
// the balance columns vanish from the shape, the readings report turnovers alone, and nothing
// anywhere says why. So the file is ASKED whether it carries the property, exactly as the accounting
// register asks for its slots, and the declared default stands where it does not.
bool ibValueMetaObjectResource::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObjectAttribute::ReadData(node))
		return false;
	if (const ibDataValue* saved = node.FindProperty(m_propertyBalance->GetName()))
		m_propertyBalance->SetNodeValue(*saved);
	// The kinds need no such care — empty IS their default, so an older file simply has none.
	if (const ibDataValue* saved = node.FindProperty(m_propertyAccountingKind->GetName()))
		m_propertyAccountingKind->SetNodeValue(*saved);
	if (const ibDataValue* saved = node.FindProperty(m_propertyAccountDimensionAccountingKind->GetName()))
		m_propertyAccountDimensionAccountingKind->SetNodeValue(*saved);
	return true;
}

bool ibValueMetaObjectResource::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObjectAttribute::WriteData(node))
		return false;
	node.SetProperty(m_propertyBalance->GetName(), m_propertyBalance->GetNodeValue());
	node.SetProperty(m_propertyAccountingKind->GetName(), m_propertyAccountingKind->GetNodeValue());
	node.SetProperty(m_propertyAccountDimensionAccountingKind->GetName(), m_propertyAccountDimensionAccountingKind->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectResource, "Resource", g_metaResourceCLSID);