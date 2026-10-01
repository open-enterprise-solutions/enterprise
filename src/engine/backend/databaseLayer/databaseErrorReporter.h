#ifndef __DATABASE_ERROR_REPORTER_H__
#define __DATABASE_ERROR_REPORTER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "databaseLayerDef.h"
#include "databaseLayerException.h"   // ibBackendDatabaseException::Kind

class BACKEND_API ibDatabaseErrorReporter
{
public:
	// ctor
	ibDatabaseErrorReporter();

	// dtor
	virtual ~ibDatabaseErrorReporter();

	const wxString& GetErrorMessage();
	int GetErrorCode();

	void ResetErrorCodes();

	// Classify the driver-native error code into a portable Kind so
	// catch-handlers can pick retry / no-retry without parsing the
	// message string. Default returns Unknown — subclasses (the
	// per-driver ibDatabaseLayer*) override with mappings rooted in
	// their native error space (FB isc_status, PG SQLSTATE int,
	// ODBC SQLSTATE).
	virtual ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const {
		(void)nativeCode;
		return ibBackendDatabaseException::Kind::Unknown;
	}

	// Driver-native SQLSTATE (5-char) when the engine reports one. PG /
	// ODBC exposes SQLSTATE; Firebird and SQLite don't, so the
	// default empty string is correct for them. Populated by per-driver
	// override at the moment the error is set.
	virtual wxString GetSqlState() const { return wxEmptyString; }

protected:

	void SetErrorMessage(const wxString& strErrorMessage);
	void SetErrorCode(int nErrorCode);

	void ThrowDatabaseException();

private:

	wxString m_strErrorMessage;
	int m_nErrorCode;
};

#endif // __DATABASE_ERROR_REPORTER_H__

