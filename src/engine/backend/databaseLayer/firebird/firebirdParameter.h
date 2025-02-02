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

class CFirebirdParameter : public CDatabaseStringConverter
{
public:
	// ctor
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const wxString& strValue, const wxCSConv* conv);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const number_t& dblValue);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, int nValue);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, double dblValue);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, bool bValue);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const wxDateTime& dateValue);
	CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const void* pData, long nDataLength);

	// dtor
	virtual ~CFirebirdParameter();

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

	int m_nParameterType;

	// A union would probably be better here
	wxString m_strValue;
	short m_sValue;
	int m_nValue;
	float m_fValue;
	double m_dblValue;

	number_t m_numValue;

	ISC_TIMESTAMP m_Date;
	bool m_bValue;
	wxMemoryBuffer m_BufferValue;
	long unsigned int m_nBufferLength;
	short m_nNullFlag;
	ISC_QUAD m_BlobId;
	isc_blob_handle m_pBlob;

	XSQLVAR* m_pParameter;
	CFirebirdInterface* m_pInterface;
};

#endif // __FIREBIRD_PARAMETER_H__

