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

class BACKEND_API ibPreparedStatement : public ibDatabaseErrorReporter, public ibDatabaseStringConverter,
	public ibDatabaseResultSetOwner
{
public:
	/// Constructor
	ibPreparedStatement() {};

	// ⭐⭐ IT STRIKES ITSELF OUT OF THE LAYER'S BOOKS. The layer keeps every statement it made so it can
	// free the ones nobody closed, and until now that list was only ever cleaned by going through the
	// layer — so a statement freed any other way left a dangling pointer in it, and closing one was
	// never the same as releasing it. Saying it here makes the two the same act: whatever destroys a
	// statement, the books agree afterwards.
	//
	// ⚠ FORGET IS NOT FREE — they are deliberately two verbs. The layer's own teardown deletes what it
	// still holds, and if this called that door instead, each delete would re-enter it and free the
	// statement twice. So this only unregisters, and deleting stays with whoever owns the pointer.
	virtual ~ibPreparedStatement();

	// Told by the layer that made it — see ibDatabaseLayer::LogStatementForCleanup. Null for a
	// statement nobody is keeping books on, which then simply has nothing to strike itself from.
	void SetOwner(class ibDatabaseLayer* owner) { m_owner = owner; }

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

	// ⭐ CLOSE ONE OF THIS STATEMENT'S RESULT SETS — and it now does. This returned false and freed
	// nothing, while two callers in loggerReader.cpp used it believing they were closing a cursor, so
	// every one of those leaked. Freeing is all it takes: the result set drops itself from this
	// statement's books on the way out.
	bool CloseResultSet(ibDatabaseResultSet* pResultSet)
	{
		if (pResultSet == nullptr)
			return false;
		delete pResultSet;
		return true;
	}

	/// A result set of this statement's is going away — drop it from the books, do NOT free it
	//
	// The other half of the pair above, and never to be called instead of it: unregistering without
	// freeing is half a release, and the half that leaks.
	void ForgetResultSet(ibDatabaseResultSet* pResultSet) override { m_ResultSets.erase(pResultSet); }

protected:
	// ⛔ IT STILL FREES NOTHING, and that is a leak kept on purpose until the other end is sorted out.
	//
	// Every driver calls this from Close(), and every driver's destructor calls Close() — so making it
	// free the cursors ties a result set's life to its statement's. The script layer holds those two
	// ends as SEPARATE ref-counted values with no link between them (ibValuePreparedStatement and
	// ibValueResultSet), so this is a guaranteed use-after-free on an ordinary shape:
	//
	//     stmt = db.PrepareStatement("select …");
	//     rs   = stmt.RunQueryWithResults();   // registered on the statement
	//     …                                    // the stmt value dies here
	//     While rs.Next() Do …                 // reading freed memory
	//
	// Today that shape leaks a cursor; with a real body it crashes. Freeing here is right and the leak
	// is real — but it cannot land until the result-set VALUE owns a link to the statement that vended
	// it (or that statement's value nulls what it handed out). Closing one cursor explicitly through
	// CloseResultSet above is unaffected and does free.
	void CloseResultSets() {}

	/// Add result set object pointer to the list for "garbage collection"
	void LogResultSetForCleanup(ibDatabaseResultSet* pResultSet)
	{
		m_ResultSets.insert(pResultSet);
		pResultSet->SetOwner(this);   // ...and it strikes itself out again when it dies
	}

private:
	StatementResultSetHashSet m_ResultSets;

	class ibDatabaseLayer* m_owner = nullptr;   // the layer keeping books on this one — see the dtor
};

#endif // __PREPARED_STATEMENT_H__

