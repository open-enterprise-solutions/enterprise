#include "metaObjectMetadata.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/metaData.h"

#include "appData.h"

bool ibValueMetaObjectConfiguration::ExecuteSystemSQLCommand()
{
	RestructureWarning("Execute system sql command");   // static facade -> the active config's ledger

	// ⭐⭐ WHICH ROUTINES AN ENGINE IS MISSING IS THE ENGINE'S OWN BUSINESS. This asked
	// `GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL` and then wrote four PostgreSQL statements
	// out by hand — the last engine-specific DDL anywhere outside `databaseLayer/`, in a file whose
	// whole subject is metadata. The driver vends it now (`CreateMissingRoutines`), so this decides
	// only WHEN — a database is being created — which is the one half that genuinely belongs here.
	//
	// A driver missing nothing inherits a no-op, so there is no branch on the engine left.
	try {
		return db_query->CreateMissingRoutines();
	}
	catch (const ibBackendException&) {
		return false;
	}
}