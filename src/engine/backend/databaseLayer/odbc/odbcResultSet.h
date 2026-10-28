#ifndef __ODBC_RESULT_SET_H__
#define __ODBC_RESULT_SET_H__

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/variant.h"   // ibVariant — a fetched cell, empty until it is read

#include "backend/databaseLayer/databaseResultSet.h"
#include "odbcInterface.h"

#include <sql.h>

class ibPreparedStatementODBC;
class ibDatabaseLayerODBC;

typedef std::unordered_set<int> IntegerSet;
typedef std::unordered_map<int, ibMemoryBuffer> BlobMap;

class ibDatabaseResultSetODBC : public ibDatabaseResultSet
{
public:
	// ctor
	ibDatabaseResultSetODBC(ibInterfaceODBC* pInterface);
	ibDatabaseResultSetODBC(ibInterfaceODBC* pInterface, ibPreparedStatementODBC* pStatement, bool bManageStatement = false, int nCol = 0);

	// dtor
	virtual ~ibDatabaseResultSetODBC();

	virtual bool Next();
	virtual void Close();

	// get field
	virtual int GetResultInt(int nField);
	virtual ibString GetResultString(int nField);
	virtual long long GetResultLong(int nField);
	virtual bool GetResultBool(int nField);
	virtual ibDateTime GetResultDate(int nField);
	virtual void* GetResultBlob(int nField, ibMemoryBuffer& buffer);
	virtual double GetResultDouble(int nField);
	virtual ibNumber GetResultNumber(int nField);
	virtual bool IsFieldNull(int nField);
	virtual int GetFieldLength(int nField);

	virtual int GetFieldLength(const ibString& strField);

	// get MetaData
	virtual ibResultSetMetaData* GetMetaData();

private:
	void RetrieveFieldData(int nField);
	void InterpretErrorCodes(long nCode, SQLHSTMT stmth_ptr = nullptr);
	virtual int LookupField(const ibString& strField);
	bool IsBlob(int nField);

	ibPreparedStatementODBC* m_pStatement;
	SQLHSTMT m_pODBCStatement;

	StringToIntMap m_FieldLookupMap;
	std::vector<ibVariant> m_fieldValues;
	// List of values that have been retrieved
	IntegerSet m_RetrievedValues;
	// List of values that are nullptr
	IntegerSet m_NullValues;

	bool m_bManageStatement;
	SQLHSTMT m_pHStmt;
	ibInterfaceODBC* m_pInterface;

	BlobMap m_BlobMap;
};

#endif // __ODBC_RESULT_SET_H__

