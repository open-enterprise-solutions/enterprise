#ifndef __MYSQL_PREPARED_STATEMENT_RESULT_SET_H__
#define __MYSQL_PREPARED_STATEMENT_RESULT_SET_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/hashmap.h>

#include "backend/databaseLayer/databaseResultSet.h"
#include "mysqlPreparedStatementParameter.h"
#include "mysqlInterface.h"

#include "engine/mysql.h"

WX_DECLARE_HASH_MAP(int, CMysqlPreparedStatementParameter*, wxIntegerHash, wxIntegerEqual, IntToMysqlParameterMap);

class CMysqlPreparedStatementResultSet : public IDatabaseResultSet
{
public:
	// ctor
	CMysqlPreparedStatementResultSet(CMysqlInterface* pInterface);
	CMysqlPreparedStatementResultSet(CMysqlInterface* pInterface, MYSQL_STMT* pStatement, bool bManageStatement = false);

	//dtor
	virtual ~CMysqlPreparedStatementResultSet();

	virtual bool Next();
	virtual void Close();

	virtual int LookupField(const wxString& strField);

	// get field
	virtual int GetResultInt(int nField);
	virtual wxString GetResultString(int nField);
	virtual long long GetResultLong(int nField);
	virtual bool GetResultBool(int nField);
	virtual wxDateTime GetResultDate(int nField);
	virtual void* GetResultBlob(int nField, wxMemoryBuffer& buffer);
	virtual double GetResultDouble(int nField);
	virtual number_t GetResultNumber(int nField);
	virtual bool IsFieldNull(int nField);

	// get MetaData
	virtual IResultSetMetaData* GetMetaData();

private:
	void ClearPreviousData();
	MYSQL_BIND* GetResultBinding(int nField);

	CMysqlInterface* m_pInterface;
	MYSQL_STMT* m_pStatement;
	MYSQL_BIND* m_pResultBindings;

	StringToIntMap m_FieldLookupMap;

	IntToMysqlParameterMap m_BindingWrappers;

	bool m_bManageStatement;
};

#endif // __MYSQL_PREPARED_STATEMENT_RESULT_SET_H__

