////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : spreadsheet object
////////////////////////////////////////////////////////////////////////////

#include "metaSpreadsheetObject.h"
#include "backend/metaData.h"



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

bool ibValueMetaObjectSpreadsheet::LoadData(ibReaderMemory& reader)
{
	return m_propertyTemplate->LoadData(reader);
}

bool ibValueMetaObjectSpreadsheet::SaveData(ibWriterMemory& writer)
{
	return m_propertyTemplate->SaveData(writer);
}

//***********************************************************************
//*                       Common Spreadsheet							*
//***********************************************************************

bool ibValueMetaObjectCommonSpreadsheet::LoadData(ibReaderMemory& reader)
{
	return m_propertyTemplate->LoadData(reader);
}

bool ibValueMetaObjectCommonSpreadsheet::SaveData(ibWriterMemory& writer)
{
	return m_propertyTemplate->SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSpreadsheet, "Template", g_metaTemplateCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonSpreadsheet, "CommonTemplate", g_metaCommonTemplateCLSID);
