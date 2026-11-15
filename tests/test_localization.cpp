// =============================================================================
// Which language a translated text is read in (core/localization/localization.cpp,
// backend/backend_localization.h).
//
// The order is the language asked for, else the configuration's own (its
// DefaultLanguage), else the first written. It used to skip the middle step,
// so a caption written `ru = '...'; en = '...';` in a configuration whose own
// language is English showed a `uk` user Russian (#471).
// =============================================================================

#include <gtest/gtest.h>
#include "core/localization/localization.h"
#include "backend/backend_localization.h"
#include "translateStateScope.h"

namespace {

// The issue's case: Russian typed first, the configuration's own language English.
const ibString kGoods(ibT("ru = 'Tovary'; en = 'Goods';"));

} // namespace

TEST(Localization, GetTranslateGetRawLocText_LanguageWritten_ReadsIt)
{
	EXPECT_EQ(ibLocalization::GetTranslateGetRawLocText(ibT("ru"), ibT("en"), kGoods), ibString(ibT("Tovary")));
}

TEST(Localization, GetTranslateGetRawLocText_LanguageMissing_ReadsTheConfigurationsOwn)
{
	EXPECT_EQ(ibLocalization::GetTranslateGetRawLocText(ibT("uk"), ibT("en"), kGoods), ibString(ibT("Goods")));
}

TEST(Localization, GetTranslateGetRawLocText_ConfigurationsOwnMissingToo_ReadsTheFirstWritten)
{
	EXPECT_EQ(ibLocalization::GetTranslateGetRawLocText(ibT("uk"), ibT("de"), kGoods), ibString(ibT("Tovary")));
}

TEST(Localization, GetTranslateGetRawLocText_NoConfigurationLanguage_ReadsTheFirstWritten)
{
	EXPECT_EQ(ibLocalization::GetTranslateGetRawLocText(ibT("uk"), ibString(), kGoods), ibString(ibT("Tovary")));
}

// A code is matched without regard to case on every step, as the language asked for always was.
TEST(Localization, GetTranslateGetRawLocText_ConfigurationsOwnInAnotherCase_IsFound)
{
	EXPECT_EQ(ibLocalization::GetTranslateGetRawLocText(ibT("uk"), ibT("EN"), kGoods), ibString(ibT("Goods")));
}

// The language asked for wins even when it holds an empty text: a cell written empty is a translation, and only
// a missing cell is replaced.
TEST(Localization, GetTranslateFromArray_LanguageWrittenEmpty_ReadsEmpty)
{
	ibLocalizationEntryArray array;
	ASSERT_TRUE(ibLocalization::CreateLocalizationArray(ibT("en = 'Goods'; uk = '';"), array));

	ibString result(ibT("stale"));
	EXPECT_TRUE(ibLocalization::GetTranslateFromArray(ibT("uk"), ibT("en"), array, result));
	EXPECT_EQ(result, ibString());
}

TEST(Localization, GetTranslateFromArray_NothingWritten_SaysNothing)
{
	ibString result(ibT("stale"));
	EXPECT_FALSE(ibLocalization::GetTranslateFromArray(ibT("uk"), ibT("en"), ibLocalizationEntryArray(), result));
	EXPECT_EQ(result, ibString());
}

// The reading every caption goes through: the user's language from the session, the configuration's own beside it.
TEST(TranslateString, GetString_UsersLanguageMissing_ReadsTheConfigurationsOwn)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibTranslateString(kGoods).GetString(), ibString(ibT("Goods")));

	ibString scratch;
	EXPECT_EQ(ibTranslateString(kGoods).GetString(scratch), ibString(ibT("Goods")));
}

TEST(TranslateString, GetTranslate_NamedLanguageMissing_ReadsTheConfigurationsOwn)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibTranslateString(kGoods).GetTranslate(ibT("de")), ibString(ibT("Goods")));
}

// An editor's box for one language stays empty when that language is: no substitute, configuration's own or not.
TEST(TranslateString, FindTranslate_LanguageMissing_FindsNothing)
{
	const ibTranslateStateScope uk(ibT("uk"), ibT("en"));
	EXPECT_EQ(ibTranslateString(kGoods).FindTranslate(ibT("uk")), ibString());
}
