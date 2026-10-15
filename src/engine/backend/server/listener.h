#ifndef __BACKEND_LISTENER_H__
#define __BACKEND_LISTENER_H__

#include "backend/backend_core.h"

#include <memory>

namespace httplib { class Server; }

// ⭐ THE LISTENER OF A SERVER OF THIS PLATFORM - TLS when backend.conf names a certificate, plain otherwise.
//
// Three servers listen on a network here - the application server for its thin clients, wenterprise-server
// for browsers, the designer's assistant access (MCP) - and all three spoke plain HTTP whatever the address.
// The transport is one thing for the process, said once, in backend.conf (the file beside the program, as
// the locale is): Certificate and PrivateKey, PEM files, relative to the file's folder. Named, a listener
// made here speaks TLS over them; not named, it speaks plain HTTP on this machine's own addresses and is
// refused on any other: the clear does not cross a network.
//
// A certificate named and not readable is a refusal, never a fallback to the clear: a server that listens in
// the clear because its certificate was mislaid is the failure this door exists to prevent. The files are read
// here, by their names in whatever script, and handed to the library as memory; a key under a password is not
// taken (there is no word for the password), and says so.
struct BACKEND_API ibTlsFiles {
	wxString m_certificate;   // PEM - the server's certificate, its chain after it
	wxString m_privateKey;    // PEM - the key of that certificate

	bool IsNamed() const { return !m_certificate.IsEmpty() || !m_privateKey.IsEmpty(); }

	// backend.conf's word: Certificate and PrivateKey, each resolved against `folder` when relative. `conf` is
	// read as written (wxCONFIG_USE_NO_ESCAPE_CHARACTERS, the caller's to set): a Windows path keeps its backslashes.
	// Read once for the process, with the rest of the file (ibApplicationHost::GetTlsFiles).
	static ibTlsFiles FromConf(const class wxConfigBase& conf, const wxString& folder);
};

// The listener for `address`: an SSLServer over the files when they are named, a plain Server when they
// are not. Refused - an ibBackendException with the reason - when only one of the files is named, when a
// named file is not there or not readable, or when nothing is named and `address` is not this machine's
// own. The caller binds it.
//
// This machine's own addresses are known as written: 127.0.0.0/8 by number, ::1, localhost - not a name
// that resolves there, since a name resolves wherever its owner says.
BACKEND_API std::unique_ptr<httplib::Server> ibMakeListener(const wxString& address, const ibTlsFiles& tls);

#endif // __BACKEND_LISTENER_H__
