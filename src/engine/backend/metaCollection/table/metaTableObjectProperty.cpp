////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "backend/metaData.h"

void ibValueMetaObjectTableData::OnPropertyRefresh()
{
	ibValueMetaObjectCompositeData::OnPropertyRefresh();

	// Use is the OWNER's question — only a hierarchical owner can restrict a tabular
	// section to folders or to items.
	HideProperty(m_propertyUse,
		dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_parent) == nullptr);
}