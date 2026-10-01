////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                         Attributes                                  *
//***********************************************************************

bool ibValueMetaObjectConstant::ReadData(const ibDataNode& node)
{
	// The value properties are the constant's own now — the attribute base used to carry them.
	m_propertyType->SetNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->SetNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));

	return ibValueMetaObjectStoredValue::ReadData(node);
}

bool ibValueMetaObjectConstant::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyType->GetName(), m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(), m_propertyFillCheck->GetNodeValue());

	return ibValueMetaObjectStoredValue::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectConstant, "Constant", g_metaConstantCLSID);
