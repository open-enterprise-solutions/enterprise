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



class ibDatabaseParameterODBC : public ibDatabaseStringConverter
{
public:
	// ctor
	ibDatabaseParameterODBC();
	ibDatabaseParameterODBC(const wxString& strValue);
	ibDatabaseParameterODBC(const ibNumber& dblValue);
	ibDatabaseParameterODBC(int nValue);
	ibDatabaseParameterODBC(double dblValue);
	ibDatabaseParameterODBC(bool bValue);
	ibDatabaseParameterODBC(const wxDateTime& dateValue);
	ibDatabaseParameterODBC(const void* pData, long nDataLength);

	// dtor
	virtual ~ibDatabaseParameterODBC() { }

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

	// ⭐ THE GATE, asked ONCE where the parameter is bound — not repeated inside the five describe
	// methods below it. Those five are the ODBC bind descriptor of ONE parameter, and each of them
	// answers `default:` with a confident, wrong claim rather than an admission: the buffer pointer
	// with a NULL (indistinguishable from a deliberate one), the C type with SQL_C_NUMERIC, the SQL
	// type with SQL_INTEGER, the size and digits with zero. Nothing in that set says "I do not know
	// what this is", so a kind this class cannot describe binds as a plausible different value.
	//
	// One question at the door instead of five answers inside — the same shape as the value gate in
	// SetNodeValue: a rule spelled per branch is a rule some branch will spell differently.
	void RequireDescribable() const;

private:
	int m_nParameterType;

	// A union would probably be better here
	TIMESTAMP_STRUCT m_DateValue;
	wxString m_strValue;
	long m_nValue;
	double m_dblValue;
	ibNumber m_numValue;
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
