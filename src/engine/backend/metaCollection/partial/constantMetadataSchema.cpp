////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constant - the STRUCTURE it declares (ContributeTables)
//
//	A constant is the odd one of the family: it declares no table of its own, only a COLUMN in the
//	shared, external sys_const. That is still a structure declaration, so it lives where every other
//	one does - beside its metadata file, not inside the query surface.
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot - the declaration it merges its column into


/////////////////////////////////////////////////////////////////////////////////////

void ibValueMetaObjectConstant::ContributeTables(ibSchemaSnapshot& out) const
{
	// sys_const is a SHARED, EXTERNAL single-row table: ALL constants are COLUMNS of the one table
	// (GetPhysicalTableName is static "sys_const"), and the table itself is owned by CreateConstantSQLTable
	// (its DEFAULT '6' PRIMARY KEY single-row scaffold the snapshot can't model). So every constant
	// merges its value column into the one external sys_const entry; the differ manages only the columns.
	static const ibMetaID kSysConstTableId = (ibMetaID)0x40000001;   // sentinel — sys_const is not a single metaobject

	// Shared find-or-create: the first constant marks it external + sets the handle; each constant Adds
	// its VALUE COLUMN. No rows — a constant is structure only.
	//
	// The column reports the constant's own id, name and type, so the differ matches it against the
	// very same column it recorded before the value column existed as an object of its own: no ALTER,
	// no re-add, no migration. If it ever starts reporting an identity of its own, the differ reads
	// that as "old column gone, new column added" — a DROP plus an ADD that empties every constant.
	out.Shared(kSysConstTableId, GetPhysicalTableName()).External(GetQueryable()).Add(m_column);
}
