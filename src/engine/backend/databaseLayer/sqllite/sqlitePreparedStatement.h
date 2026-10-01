#ifndef __SQLITE_PREPARED_STATEMENT_H__
#define __SQLITE_PREPARED_STATEMENT_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/dynarray.h>
#include "backend/databaseLayer/preparedStatement.h"

#include "engine/sqlite3.h"

WX_DEFINE_ARRAY_PTR(sqlite3_stmt*, StatementVector);

class ibDatabaseResultSet;

class ibPreparedStatementSQLite : public ibPreparedStatement
{
public:
  // ctor
  ibPreparedStatementSQLite(sqlite3* pDatabase);
  ibPreparedStatementSQLite(sqlite3* pDatabase, sqlite3_stmt* pStatement);
  ibPreparedStatementSQLite(sqlite3* pDatabase, StatementVector statements);

  // dtor
  virtual ~ibPreparedStatementSQLite();

  virtual void Close();
  
  void AddPreparedStatement(sqlite3_stmt* pStatement);

  // get field
  virtual void SetParamInt(int nPosition, int nValue);
  virtual void SetParamDouble(int nPosition, double dblValue);
  virtual void SetParamNumber(int nPosition, const ibNumber& dblValue);
  virtual void SetParamString(int nPosition, const ibString& strValue);
  virtual void SetParamNull(int nPosition);
  virtual void SetParamBlob(int nPosition, const void* pData, long nDataLength);
  virtual void SetParamDate(int nPosition, const ibDateTime& dateValue);
  virtual void SetParamBool(int nPosition, bool bValue);
  virtual int GetParameterCount();

  virtual int RunQuery();
  virtual ibDatabaseResultSet* RunQueryWithResults();

  sqlite3_stmt* GetLastStatement() { return (m_Statements.size() > 0) ? m_Statements[m_Statements.size()-1] : nullptr; }

private:
  int FindStatementAndAdjustPositionIndex(int* pPosition);
 
  sqlite3* m_pDatabase; // Database pointer needed for error messages
  StatementVector m_Statements;
};

#endif // __SQLITE_PREPARED_STATEMENT_H__

