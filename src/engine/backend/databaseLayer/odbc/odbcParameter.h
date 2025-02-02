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

#include "backend/databaseLayer/databaseStringConverter.h"



class COdbcParameter : public CDatabaseStringConverter
{
public:
	// ctor
	COdbcParameter();
	COdbcParameter(const wxString& strValue);
	COdbcParameter(const number_t& dblValue);
	COdbcParameter(int nValue);
	COdbcParameter(double dblValue);
	COdbcParameter(bool bValue);
	COdbcParameter(const wxDateTime& dateValue);
	COdbcParameter(const void* pData, long nDataLength);

	// dtor
	virtual ~COdbcParameter() { }

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
