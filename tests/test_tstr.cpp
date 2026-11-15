// =============================================================================
// Tstr — one language out of a text that carries several (systemManagerFunc.cpp).
//
// A language left unsaid is the user's: the one every caption on the screen is
// read in (ibBackendLocalization::GetUserLanguage). It used to be the first one
// written, so `Tstr(text)` showed every user the same language (#612).
//
// A language the text lacks reads in the configuration's own (its
// DefaultLanguage), and only without one in the first written (#471).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/system/systemManager.h"
#include "translateStateScope.h"

namespace {

const ibValue kTotal(ibT("en = 'Total'; ru = 'Itogo'; uk = 'Razom';"));

} // namespace

TEST(Tstr, NoLanguageReadsTheUsersLanguage)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue()), ibString(ibT("Razom")));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue(ibT(""))), ibString(ibT("Razom")));
}

TEST(Tstr, ANamedLanguageWinsOverTheUsers)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue(ibT("ru"))), ibString(ibT("Itogo")));
}

TEST(Tstr, AUserLanguageTheTextLacksReadsTheConfigurationsOwn)
{
	const ibTranslateStateScope de(ibT("de"), ibT("ru"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue()), ibString(ibT("Itogo")));
}

TEST(Tstr, ANamedLanguageTheTextLacksReadsTheConfigurationsOwn)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("ru"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue(ibT("de"))), ibString(ibT("Itogo")));
}

TEST(Tstr, WithNoConfigurationLanguage_ALanguageTheTextLacksReadsTheFirstWritten)
{
	const ibTranslateStateScope de(ibT("de"), ibT(""));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue()), ibString(ibT("Total")));
}

TEST(Tstr, APlainTextIsItself)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibValueSystemFunction::TStr(ibValue(ibT("Total")), ibValue()), ibString(ibT("Total")));
}
