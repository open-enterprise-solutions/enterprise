#include "metaObjectMetadata.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/metaData.h"

#include "appData.h"

bool CMetaObject::ExecuteSystemSQLCommand()
{
	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		//create max
		retCode = db_query->RunQuery("CREATE OR REPLACE FUNCTION max_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 < $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql; ");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;
		retCode = db_query->RunQuery("CREATE OR REPLACE AGGREGATE max(uuid) (sfunc = max_uuid, stype = uuid, combinefunc = max_uuid, parallel = safe, sortop = operator ( > ));");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;
		//create min 
		retCode = db_query->RunQuery("CREATE OR REPLACE FUNCTION min_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 > $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql; ");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;
		retCode = db_query->RunQuery("CREATE OR REPLACE AGGREGATE min(uuid) (sfunc = min_uuid, stype = uuid, combinefunc = min_uuid, parallel = safe, sortop = operator ( < ));");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;
	}
	return true;
}