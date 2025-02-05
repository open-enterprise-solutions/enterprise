#ifndef __SQLITE_RESULT_SET_METADATA_H__
#define __SQLITE_RESULT_SET_METADATA_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "backend/databaseLayer/resultSetMetaData.h"
#include "engine/sqlite3.h"

class CSqliteResultSetMetaData : public IResultSetMetaData
{
public:
  // ctor
  CSqliteResultSetMetaData(sqlite3_stmt* pStmt);

  virtual int GetColumnType(int i);
  virtual int GetColumnSize(int i);
  virtual wxString GetColumnName(int i);
  virtual int GetColumnCount();
  
private:
  sqlite3_stmt* m_pSqliteStatement;
};

#endif // __SQLITE_RESULT_SET_METADATA_H__
