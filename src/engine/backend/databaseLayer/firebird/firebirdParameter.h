#ifndef __FIREBIRD_PARAMETER_H__
#define __FIREBIRD_PARAMETER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/datetime.h>



#include "backend/databaseLayer/databaseStringConverter.h"
#include "firebirdInterface.h"

#include "engine/ibase.h"

class ibDatabaseParameterFirebird : public ibDatabaseStringConverter
{
public:
	// ctor — the parameter of its slot, NULL until a value is set
	ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar);

	// dtor
	virtual ~ibDatabaseParameterFirebird();

	// ⭐ A VALUE INTO THE SLOT, as often as the statement runs. The parameter of a position is made once and
	// given each row's value; made anew for every bind, with its string, number and buffer, it was a
	// construction and a destruction for every field of every row a prepared INSERT ran with — a share of
	// writing a payroll's 72 234 movements of its own (stack samples 2026-09-14, Debug). Each Set leaves the
	// slot as the constructor of that value used to.
	void SetNull();
	void Set(const wxString& strValue);
	void Set(const ibNumber& dblValue);
	void Set(int nValue);
	void Set(double dblValue);
	void Set(bool bValue);
	void Set(const wxDateTime& dateValue);
	void Set(const void* pData, long nDataLength);

	enum {
		PARAM_STRING = 0,
		PARAM_INT,
		PARAM_DOUBLE,
		PARAM_NUMBER,
		PARAM_DATETIME,
		PARAM_BOOL,
		PARAM_BLOB,
		PARAM_NULL
	};

	long unsigned int GetDataLength();
	long unsigned int* GetDataLengthPtr();

	const void* GetDataPtr();
	int GetParameterType();

	short GetBufferType();

	const XSQLVAR* GetFirebirdSqlVarPtr() const { return m_pParameter; }
	bool ResetBlob(isc_db_handle database, isc_tr_handle transaction);

private:

	// NO BUFFER MEANS NO SUCH PARAMETER — raise, do not write into nothing. sqldata is allocated when
	// the statement is DESCRIBED, one slot per placeholder the prepared SQL actually carries, so a
	// null is a bind addressing a parameter the statement does not have — the usual cause being a
	// schema the code believes in and the database does not. Every branch that writes into a
	// DESCRIBE-allocated buffer asks this first; the branches that point sqldata at a member of their
	// own have nothing to check.
	void RequireParameterBuffer() const;

	int m_nParameterType;

	// A union would probably be better here
	wxString m_strValue;
	short m_sValue;
	int m_nValue;
	float m_fValue;
	double m_dblValue;

	ibNumber m_numValue;

	ISC_TIMESTAMP m_Date;
	bool m_bValue;
	wxMemoryBuffer m_BufferValue;
	long unsigned int m_nBufferLength;
	short m_nNullFlag;
	ISC_QUAD m_BlobId;
	isc_blob_handle m_pBlob;

	XSQLVAR* m_pParameter;
	ibInterfaceFirebird* m_pInterface;
};

#endif // __FIREBIRD_PARAMETER_H__

