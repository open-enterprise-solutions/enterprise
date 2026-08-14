#include "metaResourceObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"


ibSelectorDataType ibValueMetaObjectResource::GetFilterDataType() const
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	if (metaObject->GetClassType() == g_metaInformationRegisterCLSID)
		return metaObject->GetFilterDataType();
	return ibSelectorDataType::ibSelectorDataType_resource;
}

// ⭐ BALANCE IS THE OWNER'S QUESTION. Only an accounting register has two sides for a figure to
// balance across; for an accumulation or an information register the word means nothing, and a
// checkbox that means nothing is worse than an absent one — somebody will tick it and expect an
// effect. The same rule SelectMode and ItemMode already follow one class up.
void ibValueMetaObjectResource::OnPropertyRefresh()
{
	ibValueMetaObjectAttribute::OnPropertyRefresh();

	const ibValueMetaObject* owner = m_parent;
	HideProperty(m_propertyBalance,
		owner == nullptr || owner->GetClassType() != g_metaAccountingRegisterCLSID);
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
		m_propertyBalance->ReadNodeValue(*saved);
	return true;
}

bool ibValueMetaObjectResource::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObjectAttribute::WriteData(node))
		return false;
	node.SetProperty(m_propertyBalance->GetName(), m_propertyBalance->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectResource, "Resource", g_metaResourceCLSID);