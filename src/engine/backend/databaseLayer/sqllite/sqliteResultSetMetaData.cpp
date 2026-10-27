#include "sqliteResultSetMetaData.h"

/*
   SPECIAL NOTE:
	The sqlite3_column_* functions are 0-based, but the JDBC ibResultSetMetaData
	functions are 1-based.  To be consistent with the 1-based JDBC (and also
	ibPreparedStatement paramters, we are using a 1-based system here.)
 */

 // ctor
ibResultSetMetaDataSQLite::ibResultSetMetaDataSQLite(sqlite3_stmt* pStmt)
{
	m_pSqliteStatement = pStmt;
}

int ibResultSetMetaDataSQLite::GetColumnType(int i)
{
	int returnType = COLUMN_UNKNOWN;
	ibString columnTypeString = ConvertFromUnicodeStream(sqlite3_column_decltype(m_pSqliteStatement, i - 1));
	columnTypeString.MakeUpper();
	if (columnTypeString.IsSameAs(ibT("INTEGER")) || columnTypeString.IsSameAs(ibT("INT")) || columnTypeString.IsSameAs(ibT("LONG")))
		returnType = COLUMN_INTEGER;
	else if (columnTypeString.IsSameAs(ibT("STRING")) || columnTypeString.StartsWith(ibT("VARCHAR")))
		returnType = COLUMN_STRING;
	else if (columnTypeString.IsSameAs(ibT("DOUBLE")) || columnTypeString.IsSameAs(ibT("FLOAT")))
		returnType = COLUMN_DOUBLE;
	else if (columnTypeString.IsSameAs(ibT("BOOL")))
		returnType = COLUMN_BOOL;
	else if (columnTypeString.IsSameAs(ibT("BLOB")))
		returnType = COLUMN_BLOB;
	else if (columnTypeString.IsSameAs(ibT("DATE")) || columnTypeString.IsSameAs(ibT("DATETIME")) || columnTypeString.IsSameAs(ibT("TIMESTAMP")))
		returnType = COLUMN_DATE;
	else
		returnType = COLUMN_UNKNOWN;

	if (returnType == COLUMN_UNKNOWN)
	{
		int columnType = sqlite3_column_type(m_pSqliteStatement, i - 1);
		switch (columnType)
		{
		case SQLITE_INTEGER:
			puts("sqlite3_column_type returned SQLITE_INTEGER");
			returnType = COLUMN_INTEGER;
			break;
		case SQLITE_FLOAT:
			returnType = COLUMN_DOUBLE;
			break;
		case SQLITE_TEXT:
			returnType = COLUMN_STRING;
			break;
		case SQLITE_BLOB:
			returnType = COLUMN_BLOB;
			break;
		case SQLITE_NULL:
			returnType = COLUMN_NULL;
			break;
		default:
			returnType = COLUMN_UNKNOWN;
			break;
		};
	}

	return returnType;
}

int ibResultSetMetaDataSQLite::GetColumnSize(int i)
{
	return -1;
}

ibString ibResultSetMetaDataSQLite::GetColumnName(int i)
{
	ibString columnName = ConvertFromUnicodeStream(sqlite3_column_name(m_pSqliteStatement, i - 1));
	return columnName;
}

int ibResultSetMetaDataSQLite::GetColumnCount()
{
	return sqlite3_column_count(m_pSqliteStatement);
}


