#include <gtest/gtest.h>

#include "wenterprise-server/pageLoadSession.h"

// GET / is a navigation. It cannot name the tab's session, so it must not
// open one: an id minted here is a row the client never adopts.
TEST(PageLoad, APageLoadDoesNotOpenASession)
{
	EXPECT_FALSE(PageLoadOpensASession());
}
