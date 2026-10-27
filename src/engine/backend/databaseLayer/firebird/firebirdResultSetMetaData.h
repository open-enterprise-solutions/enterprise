#ifndef __FIREBIRD_RESULT_SET_METADATA_H__
#define __FIREBIRD_RESULT_SET_METADATA_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "backend/databaseLayer/resultSetMetaData.h"
#include "engine/ibase.h"

class ibDatabaseResultSetMetaDataFirebird : public ibResultSetMetaData
{
public:
	// ctor
	ibDatabaseResultSetMetaDataFirebird(XSQLDA* pFields);

	// dtor
	virtual ~ibDatabaseResultSetMetaDataFirebird() { }

	virtual int GetColumnType(int i);
	virtual int GetColumnSize(int i);
	virtual ibString GetColumnName(int i);
	virtual int GetColumnCount();

private:
	XSQLVAR* GetVariable(int nField);
	XSQLDA* m_pFields;
};

#endif // __FIREBIRD_RESULT_SET_METADATA_H__
