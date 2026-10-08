#ifndef _FRMCLIENT_BACKEND_EXCEPTION_H__
#define _FRMCLIENT_BACKEND_EXCEPTION_H__

#include "core/exception.h"

// A REFUSAL — the engine's ibBackendException is a core one (core/exception.h) carrying the rest of what the engine
// knows; the client's is the core's: thrown with what was wrong, and caught where a person is told (a settings window
// that stays open on the offending setting). A file base's engine in this process raises its own, which is one too.
using ibBackendException = ibCoreException;

// A READ THAT CANNOT GO ON — the engine's ibBackendCoreException, as the client's copies of the engine raise it (a
// setting's own checks).
class ibBackendCoreException : public ibCoreException {
public:

	template <typename... Args>
	static void Error(const wxString& format, Args... args) {
		throw ibBackendCoreException(wxString::Format(format, args...));
	}

private:

	explicit ibBackendCoreException(const wxString& description) : ibCoreException(description) {}
};

#endif
