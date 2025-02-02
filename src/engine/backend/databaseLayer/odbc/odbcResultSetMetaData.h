#ifndef __ODBC_RESULT_SET_METADATA_H__
#define __ODBC_RESULT_SET_METADATA_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "backend/databaseLayer/resultSetMetaData.h"

#include "odbcInterface.h"

#include <sql.h>

class COdbcResultSetMetaData : public IResultSetMetaData
{
public:
	// ctor
	COdbcResultSetMetaData(COdbcInterface* pInterface, SQLHSTMT sqlOdbcStatement);

	// dtor
	virtual ~COdbcResultSetMetaData() {}

	virtual int GetColumnType(int i);
	virtual int GetColumnSize(int i);
	virtual wxString GetColumnName(int i);
	virtual int GetColumnCount();

private:
	COdbcInterface* m_pInterface;
	SQLHSTMT m_pOdbcStatement;
};

#endif // __ODBC_RESULT_SET_METADATA_H__
