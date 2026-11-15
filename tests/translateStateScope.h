// The thread's translate state speaking a user's language and a configuration's own for the length of a test —
// what a session's login (SetUserInfo) and its CompileRoot set. Put back as it was when the test ends.

#ifndef _IB_TEST_TRANSLATE_STATE_SCOPE_H_
#define _IB_TEST_TRANSLATE_STATE_SCOPE_H_

#include "backend/session/session.h"
#include "backend/translateState.h"

class ibTranslateStateScope {
public:
	ibTranslateStateScope(const ibString& userCode, const ibString& defaultCode)
		: m_state(ibSession::GetTranslateState()),
		m_savedUser(m_state->m_userLanguageCode), m_savedDefault(m_state->m_defLanguageCode) {
		m_state->m_userLanguageCode = userCode;
		m_state->m_defLanguageCode = defaultCode;
		m_state->Resolve();
	}
	~ibTranslateStateScope() {
		m_state->m_userLanguageCode = m_savedUser;
		m_state->m_defLanguageCode = m_savedDefault;
		m_state->Resolve();
	}
private:
	ibTranslateState* const m_state;
	const ibString m_savedUser;
	const ibString m_savedDefault;
};

#endif
