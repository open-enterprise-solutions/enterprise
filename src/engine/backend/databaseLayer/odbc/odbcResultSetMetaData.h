#ifndef __ODBC_RESULT_SET_METADATA_H__
#define __ODBC_RESULT_SET_METADATA_H__

#include "backend/databaseLayer/resultSetMetaData.h"

#include "odbcInterface.h"

#include <sql.h>

class ibDatabaseResultSetMetaDataODBC : public ibResultSetMetaData
{
public:
	// ctor
	ibDatabaseResultSetMetaDataODBC(ibInterfaceODBC* pInterface, SQLHSTMT sqlODBCStatement);

	// dtor
	virtual ~ibDatabaseResultSetMetaDataODBC() {}

	virtual int GetColumnType(int i);
	virtual int GetColumnSize(int i);
	virtual ibString GetColumnName(int i);
	virtual int GetColumnCount();

private:
	ibInterfaceODBC* m_pInterface;
	SQLHSTMT m_pODBCStatement;
};

#endif // __ODBC_RESULT_SET_METADATA_H__
