#ifndef __FIREBIRD_PARAMETER_COLLECTION_H__
#define __FIREBIRD_PARAMETER_COLLECTION_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dynarray.h>



#include "backend/databaseLayer/databaseStringConverter.h"
#include "firebirdParameter.h"
#include "firebirdInterface.h"

#include "engine/ibase.h"

WX_DEFINE_ARRAY_PTR(CFirebirdParameter*, FirebirdParameterArray);

class CFirebirdParameterCollection : public CDatabaseStringConverter
{
public:
	// ctor
	CFirebirdParameterCollection(CFirebirdInterface* pInterface, XSQLDA* pParameters);

	// dtor
	virtual ~CFirebirdParameterCollection();

	// set field
	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const number_t &numValue);
	void SetParam(int nPosition, const wxString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const wxDateTime& dateValue);
	void SetParam(int nPosition, bool bValue);
	void SetParam(int nPosition, CFirebirdParameter* pParameter);

	bool ResetBlobParameters(isc_db_handle database, isc_tr_handle transaction);
	void AllocateParameterSpace();
	void FreeParameterSpace();

private:
	FirebirdParameterArray m_Parameters;
	XSQLDA* m_FirebirdParameters;
	CFirebirdInterface* m_pInterface;
};

#endif // __FIREBIRD_PARAMETER_COLLECTION_H__

