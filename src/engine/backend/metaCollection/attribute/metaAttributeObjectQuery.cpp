#include "metaAttributeObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaData.h"

#include "backend/objCtor.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDdlStatement / ibQueryStatement / ibQueryResult (L2)
#include "backend/query/columnLayout.h"                    // DescribeColumnLayout + ibColumnSlot + ibFieldSuffix (the column-layout tier)
#include "backend/query/structureBatch.h"                  // ibStructureBatch — the per-table DDL/seed batch this fills
#include "backend/compiler/enumUnit.h"                     // enum unit — used by the field machinery below

#include <set>

// (GetSQLTypeObject removed: the attribute -> SQL-type mapping — including the last BYTEA/BLOB
//  fork — now lives in the column-layout tier (columnLayout.cpp PrimitiveType), expressed as L2
//  canonical types that the dialect dictionary spells per DBMS. ProcessAttribute builds DDL from
//  the layout, so nothing in this file maps a type to dialect SQL by hand any more.)

#include "backend/valueInfo.h"

// The DB IR builder's sort / group-by field list: the per-type data columns + the reference's _RRRef,
// skipping the _TYPE discriminator and the _RTRef target id. Over the column-layout tier (no ibSQLField).
std::vector<wxString> ibValueMetaObjectAttributeBase::GetValueFields() const
{
	std::vector<wxString> out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(this, GetMetaData()))
		if (slot.m_role != ibColumnRole::Discriminator && slot.m_role != ibColumnRole::ReferenceType)
			out.push_back(slot.m_name);
	return out;
}

// ProcessAttribute (the column-creation diff) moved to the structure tier as the free function
// DiffColumnInto(batch, srcCol, dstCol) — a metadata attribute no longer owns its DDL; it is just a
// column. See backend/query/structureBatch.{h,cpp}. The thin per-metaobject Process* forwarders (which
// only log the human-facing restructure entry) call DiffColumnInto.

///////////////////////////////////////////////////
