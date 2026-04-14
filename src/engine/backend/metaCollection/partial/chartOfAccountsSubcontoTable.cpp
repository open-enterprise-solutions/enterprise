////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : predefined SubcontoKinds tabular section for Chart of Accounts
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccountsSubcontoTable.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectSubcontoKindsTable, ibValueMetaObjectTableData);

ibValueMetaObjectSubcontoKindsTable::ibValueMetaObjectSubcontoKindsTable(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObjectTableData()
{
	SetName(name);
	SetSynonym(synonym);
	if (!comment.IsEmpty()) SetComment(comment);
}

ibValueMetaObjectSubcontoKindsTable::ibValueMetaObjectSubcontoKindsTable()
	: ibValueMetaObjectTableData()
{
}

ibValueMetaObjectSubcontoKindsTable::~ibValueMetaObjectSubcontoKindsTable()
{
}

bool ibValueMetaObjectSubcontoKindsTable::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertySubcontoKind)->LoadMeta(dataReader);
	(*m_propertyOrder)->LoadMeta(dataReader);
	(*m_propertySummaryOnly)->LoadMeta(dataReader);

	return ibValueMetaObjectTableData::LoadData(dataReader);
}

bool ibValueMetaObjectSubcontoKindsTable::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertySubcontoKind)->SaveMeta(dataWritter);
	(*m_propertyOrder)->SaveMeta(dataWritter);
	(*m_propertySummaryOnly)->SaveMeta(dataWritter);

	return ibValueMetaObjectTableData::SaveData(dataWritter);
}

bool ibValueMetaObjectSubcontoKindsTable::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectTableData::OnCreateMetaObject(metaData, flags)) return false;
	return (*m_propertySubcontoKind)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyOrder)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertySummaryOnly)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectSubcontoKindsTable::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertySubcontoKind)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyOrder)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertySummaryOnly)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectTableData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectSubcontoKindsTable::OnSaveMetaObject(int flags)
{
	if (!(*m_propertySubcontoKind)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyOrder)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectTableData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectSubcontoKindsTable::OnDeleteMetaObject()
{
	if (!(*m_propertySubcontoKind)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyOrder)->OnDeleteMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectTableData::OnDeleteMetaObject();
}

bool ibValueMetaObjectSubcontoKindsTable::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertySubcontoKind)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyOrder)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnBeforeRunMetaObject(flags)) return false;
	return ibValueMetaObjectTableData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectSubcontoKindsTable::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertySubcontoKind)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyOrder)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnAfterRunMetaObject(flags)) return false;
	return ibValueMetaObjectTableData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectSubcontoKindsTable::OnBeforeCloseMetaObject()
{
	if (!(*m_propertySubcontoKind)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyOrder)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnBeforeCloseMetaObject()) return false;
	return ibValueMetaObjectTableData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectSubcontoKindsTable::OnAfterCloseMetaObject()
{
	if (!(*m_propertySubcontoKind)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyOrder)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnAfterCloseMetaObject()) return false;
	return ibValueMetaObjectTableData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSubcontoKindsTable, "SubcontoKindsTable", g_metaSubcontoKindsTableCLSID);
