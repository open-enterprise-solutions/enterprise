#include "propertyComposition.h"

#include "backend/serialize/dataBuilder.h"   // ibDataNode / ibDataValue — the node a value can hold

// The composition's own node, carried as ONE property value. Symmetric with
// ibPropertySpreadsheet, which packs a whole spreadsheet description the same way.
bool ibPropertyComposition::ReadNodeValue(const ibDataValue& value)
{
	if (m_composition == nullptr || value.Kind() != ibDataKind::Child)
		return false;

	const std::shared_ptr<ibDataNode>& packed = value.AsChild();
	return packed != nullptr && m_composition->ReadProperty(*packed);
}

bool ibPropertyComposition::WriteNodeValue(ibDataValue& value) const
{
	if (m_composition == nullptr)
		return false;

	// The node is the composition's OWN — its variants and parameters are children of THIS node,
	// never of the metaobject that carries the property.
	std::shared_ptr<ibDataNode> packed = std::make_shared<ibDataNode>();
	if (!m_composition->WriteProperty(*packed))
		return false;

	value = ibDataValue::Child(packed);
	return true;
}
