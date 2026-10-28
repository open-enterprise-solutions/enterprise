#ifndef __POSTGRES_RESULT_SET_METADATA_H__
#define __POSTGRES_RESULT_SET_METADATA_H__

#include "backend/databaseLayer/resultSetMetaData.h"

#include "postgresInterface.h"
#include "engine/libpq-fe.h"

class ibResultSetMetaDataPostgres : public ibResultSetMetaData
{
public:
	// ctor
	ibResultSetMetaDataPostgres(ibInterfacePostgres* pInterface, PGresult* pResult);

	// dtor
	virtual ~ibResultSetMetaDataPostgres() { }

	virtual int GetColumnType(int i);
	virtual int GetColumnSize(int i);
	virtual ibString GetColumnName(int i);
	virtual int GetColumnCount();

private:
	ibInterfacePostgres* m_pInterface;
	PGresult* m_pResult;
};

#endif // __POSTGRES_RESULT_SET_METADATA_H__
