#ifndef __CORE_SERVER_LISTENER_H__
#define __CORE_SERVER_LISTENER_H__

#include "core/core.h"

#include "core/net/httpServer.h"   // ibHttpServerOptions - what a listener is started with

#include <cstdint>

#include <wx/string.h>

// ⭐ THE LISTENER OF A SERVER OF THIS PLATFORM - TLS when backend.conf names a certificate, plain otherwise.
//
// Servers listen on a network here - the application server's clients, the cluster's, the designer's assistant
// access (MCP) - and spoke plain HTTP whatever the address. The transport is one thing for the process, said once, in
// backend.conf (the file beside the program, as the locale is): Certificate and PrivateKey, PEM files, relative to the
// file's folder. Named, a listener started here speaks TLS over them; not named, it speaks plain HTTP on this
// machine's own addresses and is refused on any other: the clear does not cross a network.
//
// A certificate named and not readable is a refusal, never a fallback to the clear: a server that listens in
// the clear because its certificate was mislaid is the failure this door exists to prevent. The files are read
// here, by their names in whatever script, and handed to the server as text; a key under a password is not
// taken (there is no word for the password), and the server's Start says so.
struct CORE_API ibTlsFiles {
	wxString m_certificate;   // PEM - the server's certificate, its chain after it
	wxString m_privateKey;    // PEM - the key of that certificate

	bool IsNamed() const { return !m_certificate.IsEmpty() || !m_privateKey.IsEmpty(); }

	// backend.conf's word: Certificate and PrivateKey, each resolved against `folder` when relative. `conf` is
	// read as written (wxCONFIG_USE_NO_ESCAPE_CHARACTERS, the caller's to set): a Windows path keeps its backslashes.
	// Read once for the process, with the rest of the file (ibApplicationHost::GetTlsFiles).
	static ibTlsFiles FromConf(const class wxConfigBase& conf, const wxString& folder);
	// …the file at `path` read so, for a process that reads nothing else of it (the cluster, its tray): empty or not
	// there - nothing named.
	static ibTlsFiles FromConf(const wxString& path);
};

// What a server listening on `address`:`port` is started with (ibHttpServer::Start): HTTPS over the files, their
// text read here, when they are named; plain HTTP when they are not. Refused - an ibCoreException with the reason -
// when only one of the files is named, when a named file is not there or not readable, or when nothing is named
// and `address` is not this machine's own (ibIsLoopback, core/net/httpServer.h). Whether the files are a
// certificate and its key, Start answers.
CORE_API ibHttpServerOptions ibListenerOptions(const wxString& address, std::uint16_t port, const ibTlsFiles& tls);

#endif // __CORE_SERVER_LISTENER_H__
