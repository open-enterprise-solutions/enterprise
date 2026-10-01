#include "metaDimensionObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaCollection/partial/accountingRegister.h"   // the one owner that names a chart

// THE OWNER IS ASKED WHAT IT IS, and answers with its own class id — then the chart is read off it by
// the name of the type that has one. See the note beside the declaration.
const ibValueMetaObjectChartOfAccounts* ibValueMetaObjectDimension::GetChartOfAccounts() const
{
	const ibValueMetaObjectAccountingRegister* reg = ibValueMetaObjectAccountingRegister::OwnerOf(m_parent);
	return reg != nullptr ? reg->GetChartOfAccounts() : nullptr;
}

// A DIMENSION KEPT PER SIDE TAKES ITS SIDES WITH IT. They are attributes of the register's own
// (`<Dimension>Dr` / `<Dimension>Cr`), named and typed after this one; the tick, the name and the type
// reach them through the owner's reload, which every property change ends in — deletion does not, so it
// is said here.
bool ibValueMetaObjectDimension::OnDeleteMetaObject()
{
	if (ibValueMetaObjectAccountingRegister* reg = ibValueMetaObjectAccountingRegister::OwnerOf(m_parent))
		reg->SyncFieldSides(this);
	return ibValueMetaObjectAttribute::OnDeleteMetaObject();
}

// ⚠ AN ABSENT PROPERTY IS NOT A `false`. Every configuration written before these existed has no node
// for them, and reading one anyway hands the property an EMPTY value — which a boolean reads as "no",
// turning every stored dimension into a per-side one and changing the shape of every accounting
// register in the base. So the file is ASKED whether it carries the property, and the declared default
// stands where it does not. (The flag needs no such care: empty IS its default.)
bool ibValueMetaObjectDimension::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObjectAttribute::ReadData(node))
		return false;
	if (const ibDataValue* saved = node.FindProperty(m_propertyBalance->GetName()))
		m_propertyBalance->SetNodeValue(*saved);
	if (const ibDataValue* saved = node.FindProperty(m_propertyAccountingKind->GetName()))
		m_propertyAccountingKind->SetNodeValue(*saved);
	return true;
}

bool ibValueMetaObjectDimension::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObjectAttribute::WriteData(node))
		return false;
	node.SetProperty(m_propertyBalance->GetName(), m_propertyBalance->GetNodeValue());
	node.SetProperty(m_propertyAccountingKind->GetName(), m_propertyAccountingKind->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDimension, "Dimension", g_metaDimensionCLSID);
