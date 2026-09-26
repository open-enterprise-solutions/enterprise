#ifndef __POSTGRESQL_PARAMETER_H__
#define __POSTGRESQL_PARAMETER_H__

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



class ibDatabaseParameterPostgres : public ibDatabaseStringConverter
{
public:
	// ctor
	ibDatabaseParameterPostgres();
	ibDatabaseParameterPostgres(const wxString& strValue);
	ibDatabaseParameterPostgres(int nValue);
	ibDatabaseParameterPostgres(double dblValue);
	ibDatabaseParameterPostgres(const ibNumber &dblValue);
	ibDatabaseParameterPostgres(bool bValue);
	ibDatabaseParameterPostgres(const ibDateParts& date);   // a date by its parts (fdate.h)
	ibDatabaseParameterPostgres(const void* pData, long nDataLength);

	// dtor
	virtual ~ibDatabaseParameterPostgres() { }

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
	long* GetDataLengthPointer();

	const void* GetDataPtr();
	int GetParameterType();

	bool IsBinary();

private:
	
	int m_nParameterType;

	// A union would probably be better here
	wxString m_strValue;
	
	wxString m_strDateValue;
	wxMemoryBuffer m_BufferValue;
	wxCharBuffer m_CharBufferValue;
	
	long m_nBufferLength;
};


#endif // __POSTGRESQL_PARAMETER_H__
