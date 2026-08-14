#ifndef __PREPARED_STATEMENT_H__
#define __PREPARED_STATEMENT_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/hashset.h>

#include "databaseLayerDef.h"
#include "databaseErrorReporter.h"
#include "databaseStringConverter.h"
#include "databaseResultSet.h"
#include "databaseQueryParser.h"

WX_DECLARE_HASH_SET(ibDatabaseResultSet*, wxPointerHash, wxPointerEqual, StatementResultSetHashSet);

class BACKEND_API ibPreparedStatement : public ibDatabaseErrorReporter, public ibDatabaseStringConverter
{
public:
	/// Constructor
	ibPreparedStatement() {};

	/// Destructor
	virtual ~ibPreparedStatement() {};

	/// Close the result set (call ibDatabaseLayer::ClosePreparedStatement() instead on the statement)
	virtual void Close() = 0;

	// set parameters
	/// Set the parameter at the 1-based position to an int value
	virtual void SetParamInt(int nPosition, int nValue) = 0;
	/// Set the parameter at the 1-based position to a double value
	virtual void SetParamDouble(int nPosition, double dblValue) = 0;
	/// Set the parameter at the 1-based position to a number value
	virtual void SetParamNumber(int nPosition, const ibNumber& dblValue) = 0;
	/// Set the parameter at the 1-based position to a wxString value
	virtual void SetParamString(int nPosition, const wxString& strValue) = 0;
	/// Set the parameter at the 1-based position to a nullptr  value
	virtual void SetParamNull(int nPosition) = 0;
	/// Set the parameter at the 1-based position to a Blob value.
	///
	/// ⭐⭐ IT FORWARDS. This was an empty body — a binder that silently bound NOTHING — and no driver
	/// overrides it, so every caller that happened to hold a wxMemoryBuffer picked this overload and
	/// lost its value with no error anywhere. The audit log did exactly that: `loggerSinkSqlite.cpp`
	/// binds `ibLogEntry::details` here, so the details of every logged event were dropped at the
	/// parameter and the row was written without them.
	///
	/// An empty virtual is the worst shape for this: it is not abstract (nobody is forced to implement
	/// it), and it is not correct (it does not do the thing its name promises), so the failure is a
	/// silent wrong answer instead of a link error or an exception.
	virtual void SetParamBlob(int nPosition, const wxMemoryBuffer& buffer) {
		SetParamBlob(nPosition, buffer.GetData(), (long)buffer.GetDataLen());
	}
	/// Set the parameter at the 1-based position to a Blob value
	virtual void SetParamBlob(int nPosition, const void* pData, long nDataLength) = 0;
	/// Set the parameter at the 1-based position to a wxDateTime value
	virtual void SetParamDate(int nPosition, const wxLongLong_t& dateValue) { SetParamDate(nPosition, wxDateTime(wxLongLong(dateValue))); }
	/// Set the parameter at the 1-based position to a wxDateTime value
	virtual void SetParamDate(int nPosition, const wxDateTime& dateValue) = 0;
	/// Set the parameter at the 1-based position to a boolean value
	virtual void SetParamBool(int nPosition, bool bValue) = 0;

	virtual int GetParameterCount() = 0;

	/// Run an insert, update, or delete query on the database
	virtual int RunQuery() = 0;

	/// Run an insert, update, or delete query on the database
	virtual ibDatabaseResultSet* RunQueryWithResults() = 0;

	// function names more consistent with JDBC and wxSQLite3
	// these just provide wrappers for existing functions
	/// See RunQuery
	int ExecuteUpdate() { return RunQuery(); }
	
	/// See RunQueryWithResults
	ibDatabaseResultSet* ExecuteQuery() { return RunQueryWithResults(); }

	/// Close a result set returned by the database or a prepared statement previously
	virtual bool CloseResultSet(ibDatabaseResultSet* pResultSet) { return false; }

protected:
	/// Close all result set objects that have been generated but not yet closed
	void CloseResultSets() {};

	/// Add result set object pointer to the list for "garbage collection"
	void LogResultSetForCleanup(ibDatabaseResultSet* pResultSet) { m_ResultSets.insert(pResultSet); }

private:
	StatementResultSetHashSet m_ResultSets;
};

#endif // __PREPARED_STATEMENT_H__

