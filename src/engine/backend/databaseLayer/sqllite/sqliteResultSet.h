#ifndef __SQLITE_RESULT_SET_H__
#define __SQLITE_RESULT_SET_H__

#include "backend/databaseLayer/databaseResultSet.h"
#include "engine/sqlite3.h"

class IResultSetMetaData;
class CSqlitePreparedStatement;

class CSqliteResultSet : public IDatabaseResultSet
{
public:
	// ctor
	CSqliteResultSet();
	CSqliteResultSet(CSqlitePreparedStatement* pStatement, bool bManageStatement = false);

	// dtor
	virtual ~CSqliteResultSet();

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

	CSqlitePreparedStatement* m_pStatement;
	sqlite3_stmt* m_pSqliteStatement;

	StringToIntMap m_FieldLookupMap;

	bool m_bManageStatement;
};

#endif // __SQLITE_RESULT_SET_H__

