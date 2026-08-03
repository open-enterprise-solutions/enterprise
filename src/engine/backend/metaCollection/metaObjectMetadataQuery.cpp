#include "metaObjectMetadata.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/metaData.h"

#include "appData.h"

bool ibValueMetaObjectConfiguration::ExecuteSystemSQLCommand()
{
	RestructureWarning("Execute system sql command");   // static facade -> the active config's ledger

	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		// These are DDL: they affect no rows, so the return value is 0 and says nothing
		// about success. A driver reports failure by THROWING — that is the only signal
		// worth reading here. (This used to test the return against
		// DATABASE_LAYER_QUERY_RESULT_ERROR, which is 0, and passed only because the
		// PostgreSQL driver reported a hardcoded 1 for every statement.)
		try {
			//create max
			db_query->RunQuery("CREATE OR REPLACE FUNCTION max_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 < $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql; ");
			db_query->RunQuery("CREATE OR REPLACE AGGREGATE max(uuid) (sfunc = max_uuid, stype = uuid, combinefunc = max_uuid, parallel = safe, sortop = operator ( > ));");
			//create min
			db_query->RunQuery("CREATE OR REPLACE FUNCTION min_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 > $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql; ");
			db_query->RunQuery("CREATE OR REPLACE AGGREGATE min(uuid) (sfunc = min_uuid, stype = uuid, combinefunc = min_uuid, parallel = safe, sortop = operator ( < ));");
		}
		catch (const ibBackendException&) {
			return false;
		}
	}
	return true;
}