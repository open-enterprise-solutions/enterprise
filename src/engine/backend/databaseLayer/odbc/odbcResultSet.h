#ifndef __ODBC_RESULT_SET_H__
#define __ODBC_RESULT_SET_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/variant.h>
#include <wx/hashset.h>
#include <wx/hashmap.h>
#include <wx/dynarray.h>

#include "backend/databaseLayer/databaseResultSet.h"
#include "odbcInterface.h"

#include <sql.h>

class COdbcPreparedStatement;
class COdbcDatabaseLayer;

WX_DECLARE_OBJARRAY(wxVariant, ValuesArray);
WX_DECLARE_HASH_SET(int, wxIntegerHash, wxIntegerEqual, IntegerSet);
WX_DECLARE_HASH_MAP(int, wxMemoryBuffer, wxIntegerHash, wxIntegerEqual, BlobMap);

class COdbcResultSet : public IDatabaseResultSet
{
public:
	// ctor
	COdbcResultSet(COdbcInterface* pInterface);
	COdbcResultSet(COdbcInterface* pInterface, COdbcPreparedStatement* pStatement, bool bManageStatement = false, int nCol = 0);

	// dtor
	virtual ~COdbcResultSet();

	virtual bool Next();
	virtual void Close();

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
	virtual int GetFieldLength(int nField);

	virtual int GetFieldLength(const wxString& strField);

	// get MetaData
	virtual IResultSetMetaData* GetMetaData();

private:
	void RetrieveFieldData(int nField);
	void InterpretErrorCodes(long nCode, SQLHSTMT stmth_ptr = nullptr);
	virtual int LookupField(const wxString& strField);
	bool IsBlob(int nField);

	COdbcPreparedStatement* m_pStatement;
	SQLHSTMT m_pOdbcStatement;

	StringToIntMap m_FieldLookupMap;
	ValuesArray m_fieldValues;
	// List of values that have been retrieved
	IntegerSet m_RetrievedValues;
	// List of values that are nullptr
	IntegerSet m_NullValues;

	bool m_bManageStatement;
	SQLHSTMT m_pHStmt;
	COdbcInterface* m_pInterface;

	BlobMap m_BlobMap;
};

#endif // __ODBC_RESULT_SET_H__

