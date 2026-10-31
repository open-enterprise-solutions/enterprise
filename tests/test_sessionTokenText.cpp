////////////////////////////////////////////////////////////////////////////
//	Description : a presented token against the expected one (core/crypto/sessionToken.h TextEqual) - what is
//	accepted and what is refused. That the compare takes the same time either way is the library's word
//	(mbedtls_ct_memcmp); what is checked here is that nothing wrong slips through on the way to it. And the
//	route a token ends in (WithRoute / SplitRoute): written, read back, and nothing else taken for one.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <string>

#include "core/crypto/sessionToken.h"

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

TEST(SessionTokenText, TheRouteIsReadBackAndTheHexIsLeftAsItWas)
{
	const ibString hex = wxT("4f0e1c7a9b2d4e6f8a1b3c5d7e9f0a2b");
	EXPECT_EQ(ibSessionToken::WithRoute(hex, 0), hex) << "a server alone: the hex as it is";

	ibString read;
	unsigned route = 0;
	ibSessionToken::SplitRoute(ibSessionToken::WithRoute(hex, 2), read, route);
	EXPECT_EQ(read, hex);
	EXPECT_EQ(route, 2u);

	ibSessionToken::SplitRoute(hex, read, route);
	EXPECT_EQ(read, hex);
	EXPECT_EQ(route, 0u) << "no route: 0";
}

TEST(SessionTokenText, WhatIsNotARouteIsLeftInTheToken)
{
	for (const ibString& token : { ibString(wxT("abc.")), ibString(wxT("abc.x")), ibString(wxT("abc.0")),
			ibString(wxT("abc.65536")), ibString(wxT("abc.-1")) }) {
		ibString read;
		unsigned route = 7;
		ibSessionToken::SplitRoute(token, read, route);
		EXPECT_EQ(read, token) << token;
		EXPECT_EQ(route, 0u) << token;
	}
}
