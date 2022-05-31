#ifndef __ODBC_PARAMETER_H__
#define __ODBC_PARAMETER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/datetime.h>
#include <sql.h>

#include "compiler/compiler.h"
#include "databaseLayer/databaseStringConverter.h"

class OdbcParameter : public DatabaseStringConverter
{
public:
	// ctor
	OdbcParameter();
	OdbcParameter(const wxString& strValue);
	OdbcParameter(const number_t& dblValue);
	OdbcParameter(int nValue);
	OdbcParameter(double dblValue);
	OdbcParameter(bool bValue);
	OdbcParameter(const wxDateTime& dateValue);
	OdbcParameter(const void* pData, long nDataLength);

	// dtor
	virtual ~OdbcParameter() { }

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

	long GetDataLength();

#ifdef _WIN64
	SQLLEN* GetDataLengthPointer();
#else
	long* GetDataLengthPointer();
#endif

	void* GetDataPtr();
	SQLSMALLINT GetValueType();
	SQLSMALLINT GetParameterType();
	SQLSMALLINT GetDecimalDigits();
	SQLUINTEGER GetColumnSize();

#ifdef _WIN64
	SQLLEN* GetParameterLengthPtr(); // ???
#else
	SQLINTEGER* GetParameterLengthPtr(); // ???
#endif

	bool IsBinary();

private:
	int m_nParameterType;

	// A union would probably be better here
	TIMESTAMP_STRUCT m_DateValue;
	wxString m_strValue;
	long m_nValue;
	double m_dblValue;
	number_t m_numValue;
	wxString m_strDateValue;
	bool m_bValue;
	wxMemoryBuffer m_BufferValue;
	wxCharBuffer m_CharBufferValue;
#ifdef _WIN64
	SQLLEN m_nBufferLength;
#else
	long m_nBufferLength;
#endif

};


#endif // __ODBC_PARAMETER_H__
