#ifndef __CORE_SYSTEM_H__
#define __CORE_SYSTEM_H__

// THE SYSTEM AS IT IS - the machine this process runs on, the user it runs as, and the process's own command line and
// environment, as the system tells them: what wx/utils.h held for wx (wxGetHostName, wxGetUserId, wxGetOsDescription,
// wxSetEnv). Starting and stopping other processes is core/startProcess.h's; the language the system speaks is the
// locale's (core/localization/locale.h).

#include "core/core.h"

#include <vector>

// This computer's name, as the system calls it, without a domain - what a session, a lock and the journal name it by.
CORE_API ibString ibHostName();
// The user this process runs as, as wxGetUserId gave it.
CORE_API ibString ibUserName();
// The system in words ("Windows 11 (build 26200), 64-bit edition", "Linux 6.8.0 x86_64"), what wxGetOsDescription gave
// the journal's opening lines.
CORE_API ibString ibOsDescription();

// This process's environment, as a library it loads reads it (fbclient finds its folder by FIREBIRD).
CORE_API bool ibSetEnv(const ibString& name, const ibString& value);

// This process's command line in words, the program first - what wxApp's argv held, without asking an application
// object for it: a server has none.
CORE_API std::vector<ibString> ibCommandLineArguments();

#endif // __CORE_SYSTEM_H__
