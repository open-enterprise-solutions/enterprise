#ifndef __FIREBIRD_RESULT_SET_H__
#define __FIREBIRD_RESULT_SET_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "engine/ibase.h"

#include "backend/databaseLayer/databaseResultSet.h"
#include "firebirdInterface.h"

class ibDatabaseResultSetFirebird : public ibDatabaseResultSet
{
public:
	// ctor
	ibDatabaseResultSetFirebird(ibInterfaceFirebird* pInterface);
	ibDatabaseResultSetFirebird(ibInterfaceFirebird* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, isc_stmt_handle pStatement, XSQLDA* pFields,
		bool bManageStmt = false, bool bManageTrans = false);

	// dtor
	virtual ~ibDatabaseResultSetFirebird();

	virtual bool Next();
	virtual void Close();

	// ⭐ GIVE THE TRANSACTION BACK. Close() COMMITS the transaction it manages, which is right on the
	// success path and wrong on every failure one: the caller wants that work gone, not durable.
	//
	// The result set holds a COPY of the handle, so a caller that rolls the transaction back itself
	// and then deletes the result set has it committing an already-closed handle — two owners, one
	// transaction, and the second call operating on a handle Firebird has already invalidated. This
	// is how the ownership is handed over instead: the caller takes the transaction back FIRST, then
	// closes it however it needs to, and the result set stops knowing about it.
	void DetachTransaction() { m_pTransaction = 0; m_bManageTransaction = false; }


	virtual int LookupField(const wxString& strField);

	// get field
	virtual int GetResultInt(int nField);
	virtual wxString GetResultString(int nField);
	virtual long long GetResultLong(int nField);
	virtual bool GetResultBool(int nField);
	virtual wxDateTime GetResultDate(int nField);
	virtual void* GetResultBlob(int nField, wxMemoryBuffer& buffer);
	virtual double GetResultDouble(int nField);
	virtual ibNumber GetResultNumber(int nField);
	virtual bool IsFieldNull(int nField);

	// get MetaData
	virtual ibResultSetMetaData* GetMetaData();

private:
	bool IsNull(XSQLVAR* pVar);
	void SetDateTimeFromTm(wxDateTime& dateReturn, struct tm& timeInTm);

	void AllocateFieldSpace();
	void FreeFieldSpace();
	void PopulateFieldLookupMap();

	void InterpretErrorCodes();

	StringToIntMap m_FieldLookupMap;

	// The database and transaction handles are needed to retrieve BLOB objects
	isc_db_handle m_pDatabase;
	isc_tr_handle m_pTransaction;

	isc_stmt_handle m_pStatement;
	XSQLDA* m_pFields;

	ISC_STATUS_ARRAY m_Status;
	ibInterfaceFirebird* m_pInterface;

	bool m_bManageStatement;
	bool m_bManageTransaction;
};

#endif // __FIREBIRD_RESULT_SET_H__

