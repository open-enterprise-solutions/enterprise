#ifndef __DATABASE_ERROR_REPORTER_H__
#define __DATABASE_ERROR_REPORTER_H__

#include "databaseLayerDef.h"
#include "databaseLayerException.h"   // ibBackendDatabaseException::Kind

class BACKEND_API ibDatabaseErrorReporter
{
public:
	// ctor
	ibDatabaseErrorReporter();

	// dtor
	virtual ~ibDatabaseErrorReporter();

	const ibString& GetErrorMessage();
	int GetErrorCode();
	// The portable Kind recorded with the code, so a catch can pick retry / no-retry without parsing
	// the message. It travels with the code when one reporter hands its error to another.
	ibBackendDatabaseException::Kind GetErrorKind() const { return m_errorKind; }

	void ResetErrorCodes();

	// Driver-native SQLSTATE (5-char) when the engine reports one. PG /
	// ODBC exposes SQLSTATE; Firebird and SQLite don't, so the
	// default empty string is correct for them. Populated by per-driver
	// override at the moment the error is set.
	virtual ibString GetSqlState() const { return ibEmptyString; }

protected:

	void SetErrorMessage(const ibString& strErrorMessage);
	// A code of the platform's own (a library that did not load, no rows) — its Kind is Unknown.
	void SetErrorCode(int nErrorCode);
	// …AN ERROR OF THE ENGINE — the driver says what it is where it reads it, through its own
	// table: FB and SQLite from the native code (static ClassifyDatabaseError of the layer class),
	// PostgreSQL and ODBC from the SQLSTATE (ClassifySqlState). No default: a driver site that
	// forgets the Kind does not compile.
	void SetErrorCode(int nErrorCode, ibBackendDatabaseException::Kind kind);

	void ThrowDatabaseException();

private:

	ibString m_strErrorMessage;
	int m_nErrorCode;
	ibBackendDatabaseException::Kind m_errorKind = ibBackendDatabaseException::Kind::Unknown;
};

#endif // __DATABASE_ERROR_REPORTER_H__

