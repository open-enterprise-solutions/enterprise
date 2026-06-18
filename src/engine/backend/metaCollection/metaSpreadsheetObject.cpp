////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : spreadsheet object
////////////////////////////////////////////////////////////////////////////

#include "metaSpreadsheetObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type node data



//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectSpreadsheetBase::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectSpreadsheetBase::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           Spreadsheet                               *
//***********************************************************************

bool ibValueMetaObjectSpreadsheet::ReadData(const ibDataNode& node)
{
	m_propertyTemplate->ReadNodeValue(node.GetProperty(m_propertyTemplate->GetName()));
	return true;
}

bool ibValueMetaObjectSpreadsheet::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyTemplate->GetName(), m_propertyTemplate->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Common Spreadsheet							*
//***********************************************************************

bool ibValueMetaObjectCommonSpreadsheet::ReadData(const ibDataNode& node)
{
	m_propertyTemplate->ReadNodeValue(node.GetProperty(m_propertyTemplate->GetName()));
	return true;
}

bool ibValueMetaObjectCommonSpreadsheet::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyTemplate->GetName(), m_propertyTemplate->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSpreadsheet, "Template", g_metaTemplateCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonSpreadsheet, "CommonTemplate", g_metaCommonTemplateCLSID);
