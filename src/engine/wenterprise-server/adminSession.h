// The administration routes name the caller by one header. A cookie and ?sid=
// travel with an image and with a form posted from another site, so they do
// not count here.

#ifndef OES_ADMIN_SESSION_H_
#define OES_ADMIN_SESSION_H_

#include <string>

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include <cpp-httplib/httplib.h>

// True when X-OES-Session names a session. Otherwise 401 "no session" — a
// cookie or ?sid= on the same request is ignored.
inline bool AdministrationSessionFromHeader(const httplib::Request& req,
	httplib::Response& res, std::string& id)
{
	id = req.get_header_value("X-OES-Session");
	if (!id.empty())
		return true;
	res.status = 401;
	res.set_content("no session\n", "text/plain");
	return false;
}

#endif
