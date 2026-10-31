#ifndef __RESULT_SET_METADATA_H__
#define __RESULT_SET_METADATA_H__

#include "databaseLayerDef.h"
#include "databaseStringConverter.h"

class BACKEND_API ibResultSetMetaData : public ibDatabaseStringConverter
{
public:

	///Find column id of columns in the result set or -1
	virtual int FindColumnByName(const ibString &colName);
	/// Retrieve a column's type
	virtual int GetColumnType(int i) = 0;
	/// Retrieve a column's size
	virtual int GetColumnSize(int i) = 0;
	/// Retrieve a column's name
	virtual ibString GetColumnName(int i) = 0;
	/// Retrieve the number of columns in the result set
	virtual int GetColumnCount() = 0;

	enum {
		COLUMN_UNKNOWN = 0,
		COLUMN_NULL,
		COLUMN_INTEGER,
		COLUMN_STRING,
		COLUMN_DOUBLE,
		COLUMN_BOOL,
		COLUMN_BLOB,
		COLUMN_DATE,
	};
};

#endif // __RESULT_SET_METADATA_H__

