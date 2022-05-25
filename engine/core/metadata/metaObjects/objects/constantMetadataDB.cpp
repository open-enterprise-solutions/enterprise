////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"
#include "utils/stringUtils.h"
#include "appData.h"

int CMetaConstantObject::ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr) 
{
	return IMetaAttributeObject::ProcessAttribute(tableName, srcAttr, dstAttr);
}

bool CMetaConstantObject::CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString fieldName = GetFieldNameDB();

	int retCode = 1;

	if ((flags & createMetaTable) != 0) {
		retCode = ProcessAttribute(tableName, this, NULL);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}
	else if ((flags & updateMetaTable) != 0) {
		//if src is null then delete
		CMetaConstantObject* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			retCode = ProcessAttribute(tableName, this, dstValue);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		//if src is null then delete
		CMetaConstantObject* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			retCode = ProcessAttribute(tableName, NULL, dstValue);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				return false;
			}
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}