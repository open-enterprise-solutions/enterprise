////////////////////////////////////////////////////////////////////////////
//	Description : a presented token against the expected one (utils/sessionToken.h TextEqual) - what is
//	accepted and what is refused. That the compare takes the same time either way is the library's word
//	(mbedtls_ct_memcmp); what is checked here is that nothing wrong slips through on the way to it.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <string>

#include "backend/utils/sessionToken.h"

TEST(SessionTokenText, TheSameTextIsAcceptedAndNothingElseIs)
{
	const std::string token = "4f0e1c7a9b2d4e6f8a1b3c5d7e9f0a2b";
	EXPECT_TRUE(ibSessionToken::TextEqual(token, token));
	EXPECT_FALSE(ibSessionToken::TextEqual("4f0e1c7a9b2d4e6f8a1b3c5d7e9f0a2c", token)) << "one byte off, at the end";
	EXPECT_FALSE(ibSessionToken::TextEqual("5f0e1c7a9b2d4e6f8a1b3c5d7e9f0a2b", token)) << "one byte off, at the start";
	EXPECT_FALSE(ibSessionToken::TextEqual("4f0e1c7a9b2d4e6f", token)) << "a prefix is not the token";
	EXPECT_FALSE(ibSessionToken::TextEqual(token + "0", token)) << "nor the token with a tail";
	EXPECT_FALSE(ibSessionToken::TextEqual("", token));
}

TEST(SessionTokenText, ATokenNeverMintedMatchesNothing)
{
	EXPECT_FALSE(ibSessionToken::TextEqual("", ""));
	EXPECT_FALSE(ibSessionToken::TextEqual("anything", ""));
}
