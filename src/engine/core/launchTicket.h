#ifndef __LAUNCH_TICKET_H__
#define __LAUNCH_TICKET_H__

// A child is started with its passwords on its own stdin, as one
// length-prefixed block. The command line carries none of them, and nothing
// is written to a file. A launch from a terminal leaves stdin alone.

#include <cstdint>

#include <wx/string.h>

#include "core/core.h"

struct CORE_API ibLaunchSecrets {
	static constexpr std::uint32_t kMaxFieldBytes = 256u * 1024u;

	wxString m_dbUser;
	wxString m_dbPassword;
	wxString m_ibUser;
	wxString m_ibPassword;
};

// Start `command` the way wxExecute does, and write `secrets` on the child's
// stdin. Returns the process id, or 0 when the process did not start. The
// session that asked for the launch keeps its own copy of the password.
CORE_API long ibExecuteWithSecrets(const wxString& command, const ibLaunchSecrets& secrets);

// Read the block from stdin when stdin is not a terminal, and fold it into
// `line`. A password in the block replaces one that arrived on the command
// line. A user is filled only where the command line left it empty.
// `plaintextDb` / `plaintextIb` record that those flags were present.
CORE_API void ibApplyLaunchSecrets(ibLaunchSecrets& line, bool plaintextDb, bool plaintextIb);

// Says, once per call, that a password flag was on a command line.
CORE_API void ibNotePlaintextLaunchPassword(const wxString& flag);

#endif
