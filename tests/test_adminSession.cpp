// /admin takes the caller's session from X-OES-Session only. A cookie or
// ?sid= is how a page loads an image, and how a form on another site would
// post a kick.

#include <gtest/gtest.h>

#include <string>

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "wenterprise-server/adminSession.h"

namespace {

const char* kId = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";

httplib::Request RequestWithCookieAndSid()
{
	httplib::Request req;
	req.set_header("Cookie", std::string("oes_session=") + kId);
	req.params.emplace("sid", kId);
	return req;
}

} // namespace

TEST(AdminSession, ACookieOrQueryIdIsNotASession)
{
	httplib::Request req = RequestWithCookieAndSid();
	httplib::Response res;
	std::string id = "kept";
	EXPECT_FALSE(AdministrationSessionFromHeader(req, res, id));
	EXPECT_TRUE(id.empty());
	EXPECT_EQ(res.status, 401);
	EXPECT_EQ(res.body, "no session\n");
}

TEST(AdminSession, TheHeaderNamesTheSession)
{
	httplib::Request req = RequestWithCookieAndSid();
	req.set_header("X-OES-Session", kId);
	httplib::Response res;
	std::string id;
	EXPECT_TRUE(AdministrationSessionFromHeader(req, res, id));
	EXPECT_EQ(id, kId);
}
