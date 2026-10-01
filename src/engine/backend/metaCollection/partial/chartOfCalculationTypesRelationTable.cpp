////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : predefined relation tabular sections of a Chart of Calculation Types
////////////////////////////////////////////////////////////////////////////

#include "chartOfCalculationTypesRelationTable.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"   // tabular value-ctor register macros (registerTabularSection / _String)
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type node data

// BOUND TO ITS PARENT FOR LIFE — the section states that about ITSELF, at construction. A chart of
// calculation types always has its relations to author, so this is a fact about the class rather than
// about anything the chart is configured with.
ibValueMetaObjectCalculationTypeRelationTable::ibValueMetaObjectCalculationTypeRelationTable(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObjectTableDataRef()
{
	SetName(name);
	SetSynonym(synonym);
	if (!comment.IsEmpty()) SetComment(comment);
	SetFlag(metaPredefinedFlag);
}

ibValueMetaObjectCalculationTypeRelationTable::ibValueMetaObjectCalculationTypeRelationTable()
	: ibValueMetaObjectTableDataRef()
{
	SetFlag(metaPredefinedFlag);
}

ibValueMetaObjectCalculationTypeRelationTable::~ibValueMetaObjectCalculationTypeRelationTable()
{
}

bool ibValueMetaObjectCalculationTypeRelationTable::StampIfNeverSaved(ibMetaData* metaData)
{
	if (metaData == nullptr)
		return true;
	// The whole section was absent: the create event stamps the table, its line number and the column.
	if (GetMetaID() == 0)
		return OnCreateMetaObject(metaData, 0);
	// Only the column was absent (it was renamed): it asks for its own id, as a slot born late does.
	if ((*m_propertyCalculationType)->GetMetaID() == 0)
		return (*m_propertyCalculationType)->OnCreateMetaObject(metaData, 0);
	return true;
}

// predefined column as a Child sub-node + the base table data (NumberLine + Use).
bool ibValueMetaObjectCalculationTypeRelationTable::ReadData(const ibDataNode& node)
{
	m_propertyCalculationType->SetNodeValue(node.GetProperty(m_propertyCalculationType->GetName()));
	return ibValueMetaObjectTableDataRef::ReadData(node);
}

bool ibValueMetaObjectCalculationTypeRelationTable::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyCalculationType->GetName(), m_propertyCalculationType->GetNodeValue());
	return ibValueMetaObjectTableDataRef::WriteData(node);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectTableDataRef::OnCreateMetaObject(metaData, flags)) return false;
	return (*m_propertyCalculationType)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyCalculationType)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectTableDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyCalculationType)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectTableDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnDeleteMetaObject()
{
	if (!(*m_propertyCalculationType)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnDeleteMetaObject();
}

// A SECTION NEVER SAVED DOES NOT RUN, CLOSE OR REGISTER ANYTHING — it is not part of this configuration
// yet (StampIfNeverSaved). Said by the section, because two roads reach these doors: the chart's own
// list, and the tree's walk (RunSubtree), which asks only whether a child is deleted. Run at id 0 it
// would register its value ctors under 0, and so would the other never-saved section beside it.
bool ibValueMetaObjectCalculationTypeRelationTable::OnBeforeRunMetaObject(int flags)
{
	if (IsNeverSaved()) return true;
	if (!(*m_propertyCalculationType)->OnBeforeRunMetaObject(flags)) return false;
	// The value-ctor pair comes from the Ref base (registerTabularSectionReference + _String) — a
	// section whose rows live in a table must be created as the REFERENCE-backed value, or nothing
	// that reads it can find its owner.
	return ibValueMetaObjectTableDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnAfterRunMetaObject(int flags)
{
	if (IsNeverSaved()) return true;
	if (!(*m_propertyCalculationType)->OnAfterRunMetaObject(flags)) return false;
	return ibValueMetaObjectTableDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnBeforeCloseMetaObject()
{
	if (IsNeverSaved()) return true;
	if (!(*m_propertyCalculationType)->OnBeforeCloseMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCalculationTypeRelationTable::OnAfterCloseMetaObject()
{
	if (IsNeverSaved()) return true;
	if (!(*m_propertyCalculationType)->OnAfterCloseMetaObject()) return false;
	return ibValueMetaObjectTableDataRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCalculationTypeRelationTable, "CalculationTypeRelationTable", g_metaCalculationTypeRelationTableCLSID);
