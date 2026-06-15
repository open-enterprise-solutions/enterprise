////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibDatabaseQueryBuilder / ibQueryResult (L2 dump read)
#include "backend/metaData.h"

#include "appData.h"
#include "backend/query/structureBatch.h" // ibStructureBatch — the per-table DDL batch the value column fills
#include "backend/query/schemaBuilder.h"  // ibSchemaBuilder — flushes the batch on the local channel
#include "backend/query/schemaSnapshot.h" // ibSchemaSnapshot — ContributeTables (declarative structure)

/////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectConstant::CreateConstantSQLTable()
{
	RestructureWarning(_("Create constant table"));   // static facade -> the active config's ledger

	//create constats
	if (!db_query->TableExists(ibValueMetaObjectConstant::GetPhysicalTableName())) {

		int retCode = db_query->RunQuery("CREATE TABLE %s (RECORD_KEY CHAR DEFAULT '6' PRIMARY KEY);", ibValueMetaObjectConstant::GetPhysicalTableName());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
		// The single '6' key row is created on demand by the data restore's UPSERT (the L3-3 mover keys
		// sys_const on its RECORD_KEY primary key) — no up-front seed row needed.
	}

	return db_query->IsOpen();
}

bool ibValueMetaObjectConstant::DeleteConstantSQLTable()
{
	//create constats
	if (db_query->TableExists(ibValueMetaObjectConstant::GetPhysicalTableName())) {

		int retCode = db_query->RunQuery("DROP TABLE %s;", ibValueMetaObjectConstant::GetPhysicalTableName());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
	}

	return db_query->IsOpen();
}

/////////////////////////////////////////////////////////////////////////////////////

void ibValueMetaObjectConstant::ContributeTables(ibSchemaSnapshot& out) const
{
	// sys_const is a SHARED, EXTERNAL single-row table: ALL constants are COLUMNS of the one table
	// (GetPhysicalTableName is static "sys_const"), and the table itself is owned by CreateConstantSQLTable
	// (its DEFAULT '6' PRIMARY KEY single-row scaffold the snapshot can't model). So every constant
	// merges its value column into the one external sys_const entry; the differ manages only the columns.
	static const ibMetaID kSysConstTableId = (ibMetaID)0x40000001;   // sentinel — sys_const is not a single metaobject

	// Shared find-or-create: the first constant marks it external + sets the handle; each constant Adds
	// itself as one column (this IS the value column). No rows — a constant is structure only.
	out.Shared(kSysConstTableId, GetPhysicalTableName()).External(GetQueryable()).Add(this);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

// (Constant dump & restore are GONE — sys_const is just another ibSchemaTable in the config snapshot;
//  the L3-3 mover's EXTERNAL single-row mode UPDATEs its constant columns in place (the '6' key row is
//  pre-seeded by CreateConstantSQLTable). One source of truth: ContributeTables drives DDL AND data.)

#include "backend/objCtor.h"

/////////////////////////////////////////////////////////////////////////////////////