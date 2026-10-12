#ifndef __LAUNCH_TICKET_H__
#define __LAUNCH_TICKET_H__

// A child process is started with its database password and its application
// password in a file it reads once and deletes, mode 0600. The command line
// carries the path (`--cred-file`) and nothing that ps, Task Manager or
// /proc/pid/cmdline can show.
//
// `--password` and `--ibpwd` still parse, for a script that already passes
// them, and each use is written to the technology journal and to stderr.

#include <cstdint>

#include <wx/string.h>

#include "backend/backend.h"

struct BACKEND_API ibLaunchSecrets {
	static constexpr std::uint32_t kMaxFieldBytes = 256u * 1024u;

	wxString m_dbUser;
	wxString m_dbPassword;
	wxString m_ibUser;
	wxString m_ibPassword;
};

// Empty when nothing was written (no secrets, a field past kMaxFieldBytes,
// or the file could not be created at mode 0600).
BACKEND_API wxString ibWriteLaunchTicket(const ibLaunchSecrets& secrets);

// Reads the file, deletes it, and fills `out`. A second read finds nothing.
BACKEND_API bool ibReadLaunchTicket(const wxString& path, ibLaunchSecrets& out);

// Appends ` --cred-file="<path>"` when a ticket was written. Returns the path.
BACKEND_API wxString ibAppendLaunchTicket(wxString& command, const ibLaunchSecrets& secrets);

// Says, once per call, that a password flag was on a command line.
BACKEND_API void ibNotePlaintextLaunchPassword(const wxString& flag);

// Fills the four strings from a ticket. A password in the file replaces one
// that arrived on the command line. Users are filled only where the command
// line left them empty. `plaintextDb` / `plaintextIb` record that those flags
// were present.
BACKEND_API void ibTakeLaunchCredentials(const wxString& credFile,
                                         bool plaintextDb, bool plaintextIb,
                                         wxString& dbUser, wxString& dbPassword,
                                         wxString& ibUser, wxString& ibPassword);

#endif
