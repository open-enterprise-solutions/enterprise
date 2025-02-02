#ifndef __MYSQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__
#define __MYSQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dynarray.h>



#include "backend/databaseLayer/databaseStringConverter.h"
#include "mysqlParameter.h"

WX_DEFINE_ARRAY_PTR(CMysqlParameter*, MysqlParameterArray);

class CMysqlPreparedStatementParameterCollection : public CDatabaseStringConverter
{
public:
	// ctor
	CMysqlPreparedStatementParameterCollection();

	// dtor
	virtual ~CMysqlPreparedStatementParameterCollection();

	int GetSize();
	MYSQL_BIND* GetMysqlParameterBindings();

	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const number_t &numValue);
	void SetParam(int nPosition, const wxString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const wxDateTime& dateValue);
	void SetParam(int nPosition, bool bValue);
	void SetParam(int nPosition, CMysqlParameter* pParameter);

private:
	MysqlParameterArray m_Parameters;
};

#endif // __MYSQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__

