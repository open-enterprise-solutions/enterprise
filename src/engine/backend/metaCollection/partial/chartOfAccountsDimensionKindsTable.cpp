////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : predefined AccountDimensionKinds tabular section for Chart of Accounts
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccountsDimensionKindsTable.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"   // tabular value-ctor register macros (registerTabularSection / _String)
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type node data


// BOUND TO ITS PARENT FOR LIFE — the section states that about ITSELF, at construction. It is a fact
// about this class (a chart of accounts always has its analytics kinds), not about anything the chart
// is configured with, so it does not belong in a method that applies a binding.
ibValueMetaObjectAccountDimensionKindsTable::ibValueMetaObjectAccountDimensionKindsTable(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObjectTableDataRef()
{
	SetName(name);
	SetSynonym(synonym);
	if (!comment.IsEmpty()) SetComment(comment);
	SetFlag(metaPredefinedFlag);
}

ibValueMetaObjectAccountDimensionKindsTable::ibValueMetaObjectAccountDimensionKindsTable()
	: ibValueMetaObjectTableDataRef()
{
	SetFlag(metaPredefinedFlag);
}

ibValueMetaObjectAccountDimensionKindsTable::~ibValueMetaObjectAccountDimensionKindsTable()
{
}

// predefined columns as Child sub-nodes + the base table data (NumberLine + Use).
bool ibValueMetaObjectAccountDimensionKindsTable::ReadData(const ibDataNode& node)
{
	m_propertyAccountDimensionKind->SetNodeValue(node.GetProperty(m_propertyAccountDimensionKind->GetName()));
	m_propertySummaryOnly->SetNodeValue(node.GetProperty(m_propertySummaryOnly->GetName()));
	return ibValueMetaObjectTableDataRef::ReadData(node);
}

bool ibValueMetaObjectAccountDimensionKindsTable::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAccountDimensionKind->GetName(), m_propertyAccountDimensionKind->GetNodeValue());
	node.SetProperty(m_propertySummaryOnly->GetName(),  m_propertySummaryOnly->GetNodeValue());
	return ibValueMetaObjectTableDataRef::WriteData(node);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectTableDataRef::OnCreateMetaObject(metaData, flags)) return false;
	return (*m_propertyAccountDimensionKind)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertySummaryOnly)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAccountDimensionKind)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertySummaryOnly)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectTableDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAccountDimensionKind)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectTableDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnDeleteMetaObject()
{
	if (!(*m_propertyAccountDimensionKind)->OnDeleteMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAccountDimensionKind)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnBeforeRunMetaObject(flags)) return false;
	// The value-ctor pair comes from the Ref base (registerTabularSectionReference + _String). The
	// RAM pair was registered here by hand — copied from the RAM section — which is the other half of
	// the same mistake: a section whose rows live in a table must be created as the REFERENCE-backed
	// value, or nothing that reads it can find its owner.
	return ibValueMetaObjectTableDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAccountDimensionKind)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertySummaryOnly)->OnAfterRunMetaObject(flags)) return false;
	return ibValueMetaObjectTableDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAccountDimensionKind)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnBeforeCloseMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectAccountDimensionKindsTable::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAccountDimensionKind)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertySummaryOnly)->OnAfterCloseMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAccountDimensionKindsTable, "AccountDimensionKindsTable", g_metaAccountDimensionKindsTableCLSID);
