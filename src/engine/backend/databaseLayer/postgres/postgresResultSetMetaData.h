#ifndef __POSTGRES_RESULT_SET_METADATA_H__
#define __POSTGRES_RESULT_SET_METADATA_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "backend/databaseLayer/resultSetMetaData.h"

#include "postgresInterface.h"
#include "engine/libpq-fe.h"

class PostgresResultSetMetaData : public IResultSetMetaData
{
public:
	// ctor
	PostgresResultSetMetaData(CPostgresInterface* pInterface, PGresult* pResult);

	// dtor
	virtual ~PostgresResultSetMetaData() { }

	virtual int GetColumnType(int i);
	virtual int GetColumnSize(int i);
	virtual wxString GetColumnName(int i);
	virtual int GetColumnCount();

private:
	CPostgresInterface* m_pInterface;
	PGresult* m_pResult;
};

#endif // __POSTGRES_RESULT_SET_METADATA_H__
