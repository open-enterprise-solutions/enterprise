// =============================================================================
// Tstr — one language out of a text that carries several (systemManagerFunc.cpp).
//
// A language left unsaid is the user's: the one every caption on the screen is
// read in (ibBackendLocalization::GetUserLanguage). It used to be the first one
// written, so `Tstr(text)` showed every user the same language (#612).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/system/systemManager.h"
#include "backend/session/session.h"
#include "backend/translateState.h"

namespace {

const ibValue kTotal(ibT("en = 'Total'; ru = 'Itogo'; uk = 'Razom';"));

// The thread's translate state speaks `code` for the length of a test.
class ibUserLanguageScope {
public:
	explicit ibUserLanguageScope(const ibString& code)
		: m_state(ibSession::GetTranslateState()), m_saved(m_state->m_userLanguageCode) {
		m_state->m_userLanguageCode = code;
		m_state->Resolve();
	}
	~ibUserLanguageScope() {
		m_state->m_userLanguageCode = m_saved;
		m_state->Resolve();
	}
private:
	ibTranslateState* const m_state;
	const ibString m_saved;
};

} // namespace

TEST(Tstr, NoLanguageReadsTheUsersLanguage)
{
	const ibUserLanguageScope uk(ibT("uk"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue()), ibString(ibT("Razom")));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue(ibT(""))), ibString(ibT("Razom")));
}

TEST(Tstr, ANamedLanguageWinsOverTheUsers)
{
	const ibUserLanguageScope uk(ibT("uk"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue(ibT("ru"))), ibString(ibT("Itogo")));
}

TEST(Tstr, AUserLanguageTheTextLacksReadsTheFirstWritten)
{
	const ibUserLanguageScope de(ibT("de"));
	EXPECT_EQ(ibValueSystemFunction::TStr(kTotal, ibValue()), ibString(ibT("Total")));
}

TEST(Tstr, APlainTextIsItself)
{
	const ibUserLanguageScope uk(ibT("uk"));
	EXPECT_EQ(ibValueSystemFunction::TStr(ibValue(ibT("Total")), ibValue()), ibString(ibT("Total")));
}
