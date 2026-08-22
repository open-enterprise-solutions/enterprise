#ifndef __DATABASE_RESULT_SET_H__
#define __DATABASE_RESULT_SET_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/hashmap.h>
#include <wx/hashset.h>
#include <wx/datetime.h>

#include "databaseLayerDef.h"
#include "databaseErrorReporter.h"
#include "databaseStringConverter.h"
#include "resultSetMetaData.h"

WX_DECLARE_STRING_HASH_MAP(int, StringToIntMap);
WX_DECLARE_HASH_SET(ibResultSetMetaData*, wxPointerHash, wxPointerEqual, MetaDataHashSet);

// ⭐⭐ WHOEVER KEEPS BOOKS ON A RESULT SET. There are two of them — the layer for a result it produced
// itself, and the prepared statement for one its own cursor vends — and a result set has to be able to
// strike itself out of whichever one holds it, without either knowing about the other.
//
// It is an interface rather than two back-pointers because "who is keeping this" is ONE fact with two
// possible answers. Two nullable pointers would be a place to set the wrong one, and a place to ask
// the wrong one; there is nothing to get wrong about a single pointer to whoever it was.
class BACKEND_API ibDatabaseResultSetOwner {
public:
	/// Drop it from the books WITHOUT freeing it — called by ~ibDatabaseResultSet, nowhere else
	virtual void ForgetResultSet(class ibDatabaseResultSet* pResultSet) = 0;

protected:
	~ibDatabaseResultSetOwner() = default;   // never destroyed through this interface — it is only a role
};

class BACKEND_API ibDatabaseResultSet : public ibDatabaseErrorReporter, public ibDatabaseStringConverter
{
public:
	/// Constructor
	ibDatabaseResultSet();

	// ⭐ IT STRIKES ITSELF OUT OF ITS OWNER'S BOOKS — the twin of what ~ibPreparedStatement does, and
	// closed on the same day for the same reason. Whoever kept it held a raw pointer that only ONE
	// door removed, so a result set freed any other way left that list naming freed memory, and
	// "closed" was never quite "released". Saying it here makes the two the same act.
	//
	// ⚠ Forget is not free — see ibDatabaseResultSetOwner::ForgetResultSet. The owner's teardown deletes what
	// it still holds; if this called that, each delete would re-enter it and free the same twice.
	virtual ~ibDatabaseResultSet();

	// Told by whoever registers it — see LogResultSetForCleanup on the layer and on the statement.
	// Null for a result set nobody keeps books on, which then has nothing to strike itself from.
	void SetOwner(ibDatabaseResultSetOwner* owner) { m_owner = owner; }

	/// Move to the next record in the result set
	virtual bool Next() = 0;
	/// Close the result set (call ibDatabaseLayer::CloseResultSet() instead on the result set)
	virtual void Close() = 0;

	virtual int LookupField(const wxString& strField) = 0;

	// get field
	/// Retrieve an integer from the result set by the 1-based field index
	virtual int GetResultInt(int nField) = 0;
	/// Retrieve a wxString from the result set by the 1-based field index
	virtual wxString GetResultString(int nField) = 0;
	/// Retrieve a long from the result set by the 1-based field index
	virtual long long GetResultLong(int nField) = 0;
	/// Retrieve a boolean from the result set by the 1-based field index
	virtual bool GetResultBool(int nField) = 0;
	/// Retrieve a wxDateTime from the result set by the 1-based field index
	virtual wxDateTime GetResultDate(int nField) = 0;
	/// Retrieve a BLOB from the result set by the 1-based field index
	virtual void* GetResultBlob(int nField, wxMemoryBuffer& buffer) = 0;
	/// Retrieve a double from the result set by the 1-based field index
	virtual double GetResultDouble(int nField) = 0;
	/// Retrieve a number from the result set by the 1-based field index
	virtual ibNumber GetResultNumber(int nField) = 0;
	/// Check if a field in the current result set record is nullptr
	virtual bool IsFieldNull(int nField) = 0;

	/// Retrieve an integer from the result set by the result set column name
	virtual int GetResultInt(const wxString& strField);
	/// Retrieve a wxString from the result set by the result set column name
	virtual wxString GetResultString(const wxString& strField);
	/// Retrieve a long from the result set by the result set column name
	virtual long long GetResultLong(const wxString& strField);
	/// Retrieve a boolean from the result set by the result set column name
	virtual bool GetResultBool(const wxString& strField);
	/// Retrieve a wxDateTime from the result set by the result set column name
	virtual wxDateTime GetResultDate(const wxString& strField);
	/// Retrieve a BLOB from the result set by the result set column name
	virtual void* GetResultBlob(const wxString& strField, wxMemoryBuffer& buffer);
	/// Retrieve a double from the result set by the result set column name
	virtual double GetResultDouble(const wxString& strField);
	/// Retrieve a number from the result set by the result set column name
	virtual ibNumber GetResultNumber(const wxString& strField);
	/// Check if a field in the current result set record is nullptr
	virtual bool IsFieldNull(const wxString& strField);

	// get MetaData
	/// Retrieve the MetaData associated with this result set
	virtual ibResultSetMetaData* GetMetaData() = 0;
	/// Close MetaData previously returned by the result set
	virtual bool CloseMetaData(ibResultSetMetaData* pMetaData);

protected:
	/// Close all meta data objects that have been generated but not yet closed
	void CloseMetaData();
	/// Add meta data object pointer to the list for "garbage collection"
	void LogMetaDataForCleanup(ibResultSetMetaData* pMetaData) { m_MetaData.insert(pMetaData); }

private:
	MetaDataHashSet m_MetaData;

	ibDatabaseResultSetOwner* m_owner = nullptr;   // whoever is keeping books on this one — see the dtor
};

#endif // __DATABASE_RESULT_SET_H__

