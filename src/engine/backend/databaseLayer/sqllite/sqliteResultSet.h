#ifndef __SQLITE_RESULT_SET_H__
#define __SQLITE_RESULT_SET_H__

#include "backend/databaseLayer/databaseResultSet.h"
#include "engine/sqlite3.h"

class ibResultSetMetaData;
class ibPreparedStatementSQLite;

class ibDatabaseResultSetSQLite : public ibDatabaseResultSet
{
public:
	// ctor
	ibDatabaseResultSetSQLite();
	ibDatabaseResultSetSQLite(ibPreparedStatementSQLite* pStatement, bool bManageStatement = false);

	// dtor
	virtual ~ibDatabaseResultSetSQLite();

	virtual bool Next();
	virtual void Close();

	virtual int LookupField(const wxString& strField);

	// get field
	virtual int GetResultInt(int nField);
	virtual ibString GetResultString(int nField);
	virtual long long GetResultLong(int nField);
	virtual bool GetResultBool(int nField);
	virtual ibDateTime GetResultDate(int nField);
	virtual void* GetResultBlob(int nField, wxMemoryBuffer& buffer);
	virtual double GetResultDouble(int nField);
	virtual ibNumber GetResultNumber(int nField);
	virtual bool IsFieldNull(int nField);

	// get MetaData
	virtual ibResultSetMetaData* GetMetaData();

private:

	ibPreparedStatementSQLite* m_pStatement;
	sqlite3_stmt* m_pSqliteStatement;

	StringToIntMap m_FieldLookupMap;

	bool m_bManageStatement;
};

#endif // __SQLITE_RESULT_SET_H__

