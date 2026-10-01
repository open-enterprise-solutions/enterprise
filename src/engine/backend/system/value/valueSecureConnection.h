#ifndef __VALUE_SECURE_CONNECTION_H__
#define __VALUE_SECURE_CONNECTION_H__

#include "backend/compiler/value.h"

// ⭐⭐ A SECURE CONNECTION, SAID BY A SCRIPT - what an HTTPConnection carries to speak https://.
//
//     var trust = New CertificateFile("C:\certs\bank-ca.pem");         // whom to trust; without it, the system's store
//     var mine  = New CertificateFile("C:\certs\client.pem", "pass");  // the certificate to show, with its key, in one file
//     connection.SecureConnection = New SecureConnection(mine, trust); // both are optional
//
// Two values and no library in them: a CertificateFile is a path and a password, a SecureConnection is which
// certificate to show, whom to trust, and whether to check. What they mean on the wire is the HTTPConnection's
// business (valueHttp.cpp), where the TLS library lives.
//
// ⚠ VerifyServerCertificate IS TRUE UNLESS SAID. False keeps the encryption and drops the check - the chain AND
// the name - which is a thing for a test stand and never for a bank; the help says so in those words. The
// trusted certificates, when none are named, are the platform's own store: the Windows store, the macOS keychain,
// /etc/ssl/certs on Linux - read by the library itself.

void ibValueCertificateFile_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A PEM file: a certificate, or a certificate with its private key. The password opens the key; it is given
// once and never read back.
class BACKEND_API ibValueCertificateFile : public ibValueStaticMembers<&ibValueCertificateFile_BindNames> {
	enum Prop {
		enPath,
	};
public:

	ibValueCertificateFile();
	virtual ~ibValueCertificateFile();

	virtual bool IsEmpty() const { return m_path.empty(); }

	virtual bool Init() { return false; }                                   // a certificate file with no file is not one
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New CertificateFile(path [, password])

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	// The same for a caller in C++ (and for the tests). Open raises when the file is not there.
	void Open(const wxString& path, const wxString& password);
	const wxString& Path() const { return m_path; }
	const wxString& Password() const { return m_password; }

private:
	wxString m_path;
	wxString m_password;
};

void ibValueSecureConnection_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSecureConnection : public ibValueStaticMembers<&ibValueSecureConnection_BindNames> {
	enum Prop {
		enClientCertificate,
		enTrustedCertificates,
		enVerifyServerCertificate,
	};
public:

	ibValueSecureConnection();
	virtual ~ibValueSecureConnection();

	virtual bool Init() { return true; }                                    // New SecureConnection() - the system's trust, nothing shown
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New SecureConnection([client [, trusted]])

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	// nullptr when none is named.
	const ibValueCertificateFile* ClientCertificate() const { return m_client; }
	const ibValueCertificateFile* TrustedCertificates() const { return m_trusted; }
	bool VerifyServerCertificate() const { return m_verify; }

	void SetClientCertificate(const ibValue& value);         // a CertificateFile or Undefined, or raises
	void SetTrustedCertificates(const ibValue& value);
	void SetVerifyServerCertificate(bool verify) { m_verify = verify; }

private:
	ibValuePtr<ibValueCertificateFile> m_client;
	ibValuePtr<ibValueCertificateFile> m_trusted;
	bool                               m_verify = true;
};

#endif // !__VALUE_SECURE_CONNECTION_H__
