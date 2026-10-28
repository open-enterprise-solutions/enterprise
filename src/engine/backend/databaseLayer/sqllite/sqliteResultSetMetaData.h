#ifndef __SQLITE_RESULT_SET_METADATA_H__
#define __SQLITE_RESULT_SET_METADATA_H__

#include "backend/databaseLayer/resultSetMetaData.h"
#include "engine/sqlite3.h"

class ibResultSetMetaDataSQLite : public ibResultSetMetaData
{
public:
  // ctor
  ibResultSetMetaDataSQLite(sqlite3_stmt* pStmt);

  virtual int GetColumnType(int i);
  virtual int GetColumnSize(int i);
  virtual ibString GetColumnName(int i);
  virtual int GetColumnCount();
  
private:
  sqlite3_stmt* m_pSqliteStatement;
};

#endif // __SQLITE_RESULT_SET_METADATA_H__
